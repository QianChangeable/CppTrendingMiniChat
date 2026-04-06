#pragma once

#include <sys/epoll.h>  // epoll_create1, epoll_ctl, epoll_wait, epoll_event

#include <vector>

// 这个类封装了 Linux 的 epoll 事件模型
// 作用：让你不用在业务代码里直接反复写 epoll_create1/epoll_ctl/epoll_wait
// 适用于“一个线程同时处理多个 socket”的高并发 I/O 场景
class Epoll {
   public:
	// 默认构造：仅初始化 epoll_fd_，不创建 epoll 实例
	Epoll();

	// 析构：自动关闭 epoll_fd_
	~Epoll();

	// 禁止拷贝（同一个 epoll fd 不能被多个对象随意管理）
	Epoll(const Epoll&) = delete;
	Epoll& operator=(const Epoll&) = delete;

	// 创建 epoll 实例
	// 成功：true
	// 失败：false
	bool create();

	// 添加监听 fd
	// fd：要监听的文件描述符（监听 socket 或客户端 socket）
	// events：监听哪些事件（如 EPOLLIN/EPOLLOUT/EPOLLET）
	bool add(int fd, uint32_t events);

	// 修改已监听 fd 的事件类型
	// 常用于“状态切换”，比如：
	// 只读 -> 读写
	bool modify(int fd, uint32_t events);

	// 从 epoll 里移除一个 fd
	// 一般在连接关闭前后调用
	bool remove(int fd);

	// 等待事件发生
	// ready_events：输出参数，返回本轮就绪事件列表
	// timeout_ms：
	//   -1  阻塞等待（一直等）
	//    0  立即返回（轮询）
	//  > 0  最多等待指定毫秒
	// max_events：单次最多取多少个事件
	// 返回值：
	//   >0 本次就绪事件个数
	//    0 超时（没有事件）
	//   -1 出错
	int wait(std::vector<epoll_event>& ready_events, int timeout_ms = -1, int max_events = 1024);

	// 关闭 epoll fd
	// 可重复调用（幂等）
	void close();

   private:
	// epoll 实例对应的 fd
	// >=0 有效
	// -1 无效（未创建或已关闭）
	int epoll_fd_;
};