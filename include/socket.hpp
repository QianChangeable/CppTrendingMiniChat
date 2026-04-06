#pragma once

#include <netinet/in.h>

#include <string>

class Socket {
   public:
	// 构造函数
	Socket();

	// 单参数构造函数，加 explicit 防止隐式转换
	explicit Socket(int fd);

	// 析构函数（自动释放 fd）
	~Socket();

	// 禁止拷贝构造（避免多个对象管理同一个 fd）
	Socket(const Socket&) = delete;

	// 禁止拷贝赋值
	Socket& operator=(const Socket&) = delete;

	// 移动构造（转移 fd 所有权）
	Socket(Socket&& other) noexcept;

	// 移动赋值（转移 fd 所有权）
	Socket& operator=(Socket&& other) noexcept;

	// 创建 AF_INET + SOCK_STREAM 的 TCP 套接字
	bool create();

	// 绑定 ip:port（服务端）
	bool bind(unsigned short port, const std::string& ip = "0.0.0.0");

	// 开始监听（服务端）
	bool listen(int backlog = 128);

	// 接受连接，返回客户端 socket_fd，失败返回 -1
	// client_addr 可选，用于获取对端地址信息
	int accept(sockaddr_in* client_addr = nullptr);

	// 连接服务器（客户端）
	bool connect(const std::string& ip, unsigned short port);

	// 发送数据
	ssize_t send(const void* data, size_t data_size, int flags = 0);

	// 接收数据
	ssize_t recv(void* buffer, size_t buffer_size, int flags = 0);

	// 设置非阻塞模式（epoll 常用）
	bool set_non_blocking();

	// 关闭连接（可重复调用）
	void close();

	// 获取底层 fd
	int fd() const;

	// fd 是否有效
	bool valid() const;

   private:
	int fd_;
};