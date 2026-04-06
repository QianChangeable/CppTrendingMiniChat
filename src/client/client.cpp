#include <errno.h>

#include <iostream>
#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>

#include <string>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vector>

#include "epoll.hpp"
#include "socket.hpp"

using json = nlohmann::json;

static const char* k_server_ip = "127.0.0.1";
static const unsigned short k_server_port = 8888;
static const size_t k_read_buffer_size = 4096;

bool is_interactive_terminal() {
	return ::isatty(STDIN_FILENO) == 1;
}

void print_input_prompt() {
	if (is_interactive_terminal()) {
		std::cout << "> ";
		std::cout.flush();
	}
}

void disconnect_client(Socket& socket_client, const std::string& reason) {
	SPDLOG_INFO("{}", reason);
	if (socket_client.valid()) {
		::shutdown(socket_client.fd(), SHUT_RDWR);
		socket_client.close();
	}
	SPDLOG_INFO("client disconnected, bye");
}

// 安全解析 JSON，不抛异常
bool try_parse_json(const std::string& text, json& out_json) {
	try {
		out_json = json::parse(text);
		return true;
	} catch (...) {
		return false;
	}
}

// 从键盘读取一行，封装为 JSON 发给服务器
bool handle_stdin_and_send(Socket& socket_client) {
	std::string input_line;
	if (!std::getline(std::cin, input_line)) {
		disconnect_client(socket_client, "stdin closed, active disconnect");
		return false;
	}

	if (input_line == "quit") {
		disconnect_client(socket_client, "quit command received, active disconnect");
		return false;
	}

	// 客户端发给服务器的协议：
	// {"type":"chat","text":"用户输入"}
	json outbound_json = {
	    {"type", "chat"},
	    {"text", input_line},
	};

	std::string wire_text = outbound_json.dump();
	wire_text.push_back('\n');  // 一条 JSON 一行

	size_t sent_total = 0;
	while (sent_total < wire_text.size()) {
		ssize_t sent_now = socket_client.send(wire_text.data() + sent_total, wire_text.size() - sent_total, 0);

		if (sent_now > 0) {
			sent_total += static_cast<size_t>(sent_now);
			continue;
		}

		if (sent_now < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			::usleep(1000);
			continue;
		}

		SPDLOG_ERROR("send failed, errno={}", errno);
		disconnect_client(socket_client, "send failed, active disconnect");
		return false;
	}

	return true;
}

// 从接收缓存中按 '\n' 切行，再逐行 JSON 解析和展示
bool process_server_lines(std::string& recv_buffer) {
	bool printed_chat = false;

	while (true) {
		size_t newline_pos = recv_buffer.find('\n');
		if (newline_pos == std::string::npos) {
			break;
		}

		std::string one_line = recv_buffer.substr(0, newline_pos);
		recv_buffer.erase(0, newline_pos + 1);

		if (one_line.empty()) {
			continue;
		}

		json inbound_json;
		if (!try_parse_json(one_line, inbound_json)) {
			SPDLOG_WARN("invalid json from server: {}", one_line);
			continue;
		}

		std::string type = inbound_json.value("type", "");
		if (type == "chat") {
			std::string from = inbound_json.value("from", "unknown");
			std::string text = inbound_json.value("text", "");
			std::cout << from << ": " << text << "\n";
			std::cout.flush();
			printed_chat = true;
		} else {
			SPDLOG_WARN("unknown server message type: {}", type);
		}
	}

	return printed_chat;
}

// 接收服务器数据
bool handle_socket_and_print(Socket& socket_client, std::string& recv_buffer) {
	char read_buffer[k_read_buffer_size];

	while (true) {
		ssize_t n = socket_client.recv(read_buffer, sizeof(read_buffer), 0);

		if (n > 0) {
			recv_buffer.append(read_buffer, static_cast<size_t>(n));
			if (process_server_lines(recv_buffer)) {
				print_input_prompt();
			}
			continue;
		}

		if (n == 0) {
			SPDLOG_INFO("server closed connection");
			return false;
		}

		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			break;
		}

		SPDLOG_ERROR("recv failed, errno={}", errno);
		return false;
	}

	return true;
}

int main() {
	spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");

	Socket socket_client;
	if (!socket_client.create()) {
		SPDLOG_ERROR("socket create failed");
		return 1;
	}

	// 为了新手好理解：先阻塞 connect，成功后再切非阻塞
	if (!socket_client.connect(k_server_ip, k_server_port)) {
		SPDLOG_ERROR("connect failed, server={}:{}", k_server_ip, k_server_port);
		return 1;
	}

	if (!socket_client.set_non_blocking()) {
		SPDLOG_ERROR("set_non_blocking failed");
		return 1;
	}

	SPDLOG_INFO("connected to {}:{}", k_server_ip, k_server_port);
	SPDLOG_INFO("type message and Enter to send, type 'quit' to exit");
	print_input_prompt();

	Epoll epoll;
	if (!epoll.create()) {
		SPDLOG_ERROR("epoll.create failed");
		return 1;
	}

	if (!epoll.add(STDIN_FILENO, EPOLLIN)) {
		SPDLOG_ERROR("epoll.add stdin failed");
		return 1;
	}

	if (!epoll.add(socket_client.fd(), EPOLLIN)) {
		SPDLOG_ERROR("epoll.add socket failed");
		return 1;
	}

	std::string recv_buffer;

	while (true) {
		std::vector<epoll_event> ready_events;
		int ready_count = epoll.wait(ready_events, -1, 16);
		if (ready_count < 0) {
			SPDLOG_ERROR("epoll.wait failed");
			continue;
		}

		for (const auto& event : ready_events) {
			int event_fd = event.data.fd;
			uint32_t event_mask = event.events;

			if (event_mask & (EPOLLERR | EPOLLHUP)) {
				if (event_fd == socket_client.fd()) {
					SPDLOG_ERROR("socket error/hup");
					return 0;
				}
			}

			if ((event_mask & EPOLLIN) && event_fd == STDIN_FILENO) {
				if (!handle_stdin_and_send(socket_client)) {
					return 0;
				}
				print_input_prompt();
				continue;
			}

			if ((event_mask & EPOLLIN) && event_fd == socket_client.fd()) {
				if (!handle_socket_and_print(socket_client, recv_buffer)) {
					return 0;
				}
				continue;
			}
		}
	}

	return 0;
}