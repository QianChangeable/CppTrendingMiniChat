#include "socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

// 无参构造
Socket::Socket() : fd_(-1) {
}

// 单参数构造函数
Socket::Socket(int fd) : fd_(fd) {
}

Socket::~Socket() {
	// RAII，对象析构时自动关闭socket_fd
	close();
}

// 拷贝赋值
Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) {
	other.fd_ = -1;
}

// 移动赋值
Socket& Socket::operator=(Socket&& other) noexcept {
	if (this != &other) {
		close();  // 先释放自己当前持有的
		fd_ = other.fd_;
		other.fd_ = -1;
	}

	// 返回值
	return *this;
}

// 创建 IPv4 TCP socket
bool Socket::create() {
	// 如果已经创建过，直接返回成功
	if (fd_ >= 0) {
		return true;
	}

	fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
	return fd_ >= 0;
}

// 绑定 ip 和端口
bool Socket::bind(unsigned short port, const std::string& ip) {
	if (fd_ < 0) {
		return false;
	}

	// 打开地址复用，避免重启时报 Address already in use
	int reuse_addr = 1;
	::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

	sockaddr_in socket_addr{};
	socket_addr.sin_family = AF_INET;
	socket_addr.sin_port = htons(port);

	// 点分十进制 IP -> 二进制网络格式
	if (::inet_pton(AF_INET, ip.c_str(), &socket_addr.sin_addr) <= 0) {
		return false;
	}

	return ::bind(fd_, reinterpret_cast<sockaddr*>(&socket_addr), sizeof(socket_addr)) == 0;
}

// 监听连接
bool Socket::listen(int backlog) {
	if (fd_ < 0) {
		return false;
	}
	return ::listen(fd_, backlog) == 0;
}

// 接收一个客户端连接
int Socket::accept(sockaddr_in* client_addr) {
	if (fd_ < 0) {
		return -1;
	}

	sockaddr_in peer_addr{};
	socklen_t peer_addr_len = sizeof(peer_addr);

	// accept成功返回新的客户端socket_fd
	int client_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer_addr), &peer_addr_len);

	// 如果调用方需要，回填客户端地址
	if (client_fd >= 0 && client_addr != nullptr) {
		*client_addr = peer_addr;
	}

	return client_fd;
}

// 连接到指定服务器
bool Socket::connect(const std::string& ip, unsigned short port) {
	if (fd_ < 0) {
		return false;
	}

	sockaddr_in server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);

	// 文本 IP -> 二进制
	if (::inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
		return false;
	}

	return ::connect(fd_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == 0;
}
// 发送数据
ssize_t Socket::send(const void* data, size_t data_size, int flags) {
	if (fd_ < 0) {
		return -1;
	}
	return ::send(fd_, data, data_size, flags);
}

// 接收数据
ssize_t Socket::recv(void* buffer, size_t buffer_size, int flags) {
	if (fd_ < 0) {
		return -1;
	}
	return ::recv(fd_, buffer, buffer_size, flags);
}

// 设置非阻塞：在原 flags 基础上 OR O_NONBLOCK
bool Socket::set_non_blocking() {
	if (fd_ < 0) {
		return false;
	}

	int fd_flags = ::fcntl(fd_, F_GETFL, 0);
	if (fd_flags < 0) {
		return false;
	}

	return ::fcntl(fd_, F_SETFL, fd_flags | O_NONBLOCK) == 0;
}

// 关闭 fd，幂等
void Socket::close() {
	if (fd_ >= 0) {
		::close(fd_);
		fd_ = -1;
	}
}

// 返回底层 fd
int Socket::fd() const {
	return fd_;
}

// 判断 fd 是否有效
bool Socket::valid() const {
	return fd_ >= 0;
}