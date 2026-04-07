#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>

#include <nlohmann/json.hpp>
#include <queue>

#include <spdlog/spdlog.h>

#include <string>

#include <sys/epoll.h>
#include <unistd.h>

#include <unordered_map>
#include <vector>

#include "epoll.hpp"
#include "socket.hpp"

using json = nlohmann::json;

static const unsigned short k_server_port = 8888;
static const size_t k_read_buffer_size = 4096;

struct ClientSession {
	int fd;
	std::string nickname;
	std::string recv_buffer;
	std::queue<std::string> send_queue;
};

static std::unordered_map<int, ClientSession> client_sessions;

std::string ToIpPortString(const sockaddr_in& client_addr) {
	char ip_buffer[INET_ADDRSTRLEN] = {0};
	::inet_ntop(AF_INET, &client_addr.sin_addr, ip_buffer, sizeof(ip_buffer));
	unsigned short port = ntohs(client_addr.sin_port);
	return std::string(ip_buffer) + ":" + std::to_string(port);
}

// 把字符串安全解析成 JSON
// 解析失败返回 false，不抛异常
bool TryParseJson(const std::string& text, json& out_json) {
	try {
		out_json = json::parse(text);
		return true;
	} catch (...) {
		return false;
	}
}

bool AddClient(Epoll& epoll, int client_fd) {
	int fd_flags = ::fcntl(client_fd, F_GETFL, 0);
	if (fd_flags < 0 || ::fcntl(client_fd, F_SETFL, fd_flags | O_NONBLOCK) != 0) {
		SPDLOG_ERROR("set_non_blocking failed, fd={}", client_fd);
		return false;
	}

	if (!epoll.add(client_fd, EPOLLIN)) {
		SPDLOG_ERROR("epoll add failed, fd={}", client_fd);
		return false;
	}

	ClientSession session;
	session.fd = client_fd;
	session.nickname = "user_" + std::to_string(client_fd);
	client_sessions[client_fd] = std::move(session);

	SPDLOG_INFO("client online, fd={}", client_fd);
	return true;
}

void RemoveClient(Epoll& epoll, int client_fd) {
	epoll.remove(client_fd);
	::close(client_fd);
	client_sessions.erase(client_fd);

	SPDLOG_INFO("client offline, fd={}", client_fd);
}

void EnqueueJsonToClient(Epoll& epoll, int client_fd, const json& message_json) {
	auto it = client_sessions.find(client_fd);
	if (it == client_sessions.end()) {
		return;
	}

	// 每条 JSON 用 '\n' 分隔，便于按行拆包
	std::string wire_text = message_json.dump();
	wire_text.push_back('\n');

	it->second.send_queue.push(std::move(wire_text));

	// 队列里有待发送数据时，打开 EPOLLOUT
	epoll.modify(client_fd, EPOLLIN | EPOLLOUT);
}

void BroadcastJson(Epoll& epoll, const json& message_json, int exclude_fd) {
	for (auto& entry : client_sessions) {
		int client_fd = entry.first;
		if (client_fd == exclude_fd) {
			continue;
		}
		EnqueueJsonToClient(epoll, client_fd, message_json);
	}
}

// 处理一条来自客户端的 JSON 消息
void ProcessOneClientJson(Epoll& epoll, ClientSession& sender, const json& inbound_json) {
	// 简单协议：
	// {
	//   "type": "chat",
	//   "text": "hello"
	// }
	std::string type = inbound_json.value("type", "");
	if (type != "chat") {
		SPDLOG_WARN("unknown message type from fd={}, type={}", sender.fd, type);
		return;
	}

	std::string text = inbound_json.value("text", "");
	if (text.empty()) {
		return;
	}

	SPDLOG_INFO("chat from {}: {}", sender.nickname, text);

	// 服务端广播消息格式：
	// {
	//   "type": "chat",
	//   "from": "user_xxx",
	//   "text": "hello"
	// }
	json outbound_json = {
	    {"type", "chat"},
	    {"from", sender.nickname},
	    {"text", text},
	};

	BroadcastJson(epoll, outbound_json, sender.fd);
}

// 从 recv_buffer 里按 '\n' 取出完整一行，再当 JSON 处理
void ProcessRecvBuffer(Epoll& epoll, ClientSession& client) {
	while (true) {
		size_t newline_pos = client.recv_buffer.find('\n');
		if (newline_pos == std::string::npos) {
			break;
		}

		std::string one_line = client.recv_buffer.substr(0, newline_pos);
		client.recv_buffer.erase(0, newline_pos + 1);

		if (one_line.empty()) {
			continue;
		}

		json inbound_json;
		if (!TryParseJson(one_line, inbound_json)) {
			SPDLOG_WARN("invalid json from fd={}, raw={}", client.fd, one_line);
			continue;
		}

		ProcessOneClientJson(epoll, client, inbound_json);
	}
}

void HandleClientReadable(Epoll& epoll, int client_fd) {
	auto it = client_sessions.find(client_fd);
	if (it == client_sessions.end()) {
		return;
	}
	ClientSession& client = it->second;

	char read_buffer[k_read_buffer_size];

	while (true) {
		ssize_t n = ::recv(client_fd, read_buffer, sizeof(read_buffer), 0);

		if (n > 0) {
			client.recv_buffer.append(read_buffer, static_cast<size_t>(n));
			ProcessRecvBuffer(epoll, client);
			continue;
		}

		if (n == 0) {
			RemoveClient(epoll, client_fd);
			return;
		}

		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			break;
		}

		SPDLOG_ERROR("recv failed, fd={}, errno={}", client_fd, errno);
		RemoveClient(epoll, client_fd);
		return;
	}
}

void HandleClientWritable(Epoll& epoll, int client_fd) {
	auto it = client_sessions.find(client_fd);
	if (it == client_sessions.end()) {
		return;
	}
	ClientSession& client = it->second;

	while (!client.send_queue.empty()) {
		std::string& front = client.send_queue.front();

		ssize_t n = ::send(client_fd, front.data(), front.size(), 0);

		if (n > 0) {
			if (static_cast<size_t>(n) == front.size()) {
				client.send_queue.pop();
			} else {
				front.erase(0, static_cast<size_t>(n));
				break;
			}
			continue;
		}

		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			break;
		}

		SPDLOG_ERROR("send failed, fd={}, errno={}", client_fd, errno);
		RemoveClient(epoll, client_fd);
		return;
	}

	if (client_sessions.find(client_fd) != client_sessions.end() && client.send_queue.empty()) {
		epoll.modify(client_fd, EPOLLIN);
	}
}

int main() {
	// 设置日志格式：时间 + 级别 + 消息
	spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");

	Socket listen_socket;
	if (!listen_socket.create()) {
		SPDLOG_ERROR("listen_socket.create failed");
		return 1;
	}

	if (!listen_socket.set_non_blocking()) {
		SPDLOG_ERROR("listen_socket.set_non_blocking failed");
		return 1;
	}

	if (!listen_socket.bind(k_server_port, "0.0.0.0")) {
		SPDLOG_ERROR("listen_socket.bind failed");
		return 1;
	}

	if (!listen_socket.listen(128)) {
		SPDLOG_ERROR("listen_socket.listen failed");
		return 1;
	}

	SPDLOG_INFO("server started at 0.0.0.0:{}", k_server_port);

	Epoll epoll;
	if (!epoll.create()) {
		SPDLOG_ERROR("epoll.create failed");
		return 1;
	}

	if (!epoll.add(listen_socket.fd(), EPOLLIN)) {
		SPDLOG_ERROR("epoll.add listen_socket failed");
		return 1;
	}

	while (true) {
		std::vector<epoll_event> ready_events;
		int ready_count = epoll.wait(ready_events, 1000, 1024);
		if (ready_count < 0) {
			SPDLOG_ERROR("epoll.wait failed");
			continue;
		}

		for (const auto& event : ready_events) {
			int event_fd = event.data.fd;
			uint32_t event_mask = event.events;

			if (event_mask & (EPOLLERR | EPOLLHUP)) {
				if (event_fd != listen_socket.fd()) {
					RemoveClient(epoll, event_fd);
				}
				continue;
			}

			if (event_fd == listen_socket.fd()) {
				while (true) {
					sockaddr_in client_addr{};
					int client_fd = listen_socket.accept(&client_addr);

					if (client_fd >= 0) {
						SPDLOG_INFO("accept client fd={}, addr={}", client_fd, ToIpPortString(client_addr));

						if (!AddClient(epoll, client_fd)) {
							SPDLOG_ERROR("AddClient failed, errno={}, msg={}", errno, strerror(errno));
							::close(client_fd);
						}
						continue;
					}

					if (errno == EAGAIN || errno == EWOULDBLOCK) {
						break;
					}

					SPDLOG_ERROR("accept failed, errno={}", errno);
					break;
				}
				continue;
			}

			if (event_mask & EPOLLIN) {
				HandleClientReadable(epoll, event_fd);
			}

			if ((event_mask & EPOLLOUT) && client_sessions.find(event_fd) != client_sessions.end()) {
				HandleClientWritable(epoll, event_fd);
			}
		}
	}

	return 0;
}