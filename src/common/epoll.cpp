#include "epoll.hpp"

#include <unistd.h>  // close

// 默认构造：先把 epoll_fd_ 设为无效
Epoll::Epoll() : epoll_fd_(-1) {
}

// 析构自动释放资源
Epoll::~Epoll() {
	close();
}

// 创建 epoll 实例
bool Epoll::create() {
	// 如果已经创建过，直接返回成功
	if (epoll_fd_ >= 0) {
		return true;
	}

	// epoll_create1(0) 创建一个 epoll 实例，返回 epoll fd
	// 成功 >=0，失败 -1
	epoll_fd_ = ::epoll_create1(0);
	return epoll_fd_ >= 0;
}

// 添加 fd 到 epoll 监听集合
bool Epoll::add(int fd, uint32_t events) {
	// 没有有效 epoll fd，无法 add
	if (epoll_fd_ < 0) {
		return false;
	}

	// epoll_event 用来告诉内核“监听什么事件”以及“事件发生时回传什么数据”
	epoll_event event{};
	event.events = events;  // 比如 EPOLLIN（可读）
	event.data.fd = fd;     // 把 fd 存进 data，wait 返回时可直接取出

	// EPOLL_CTL_ADD：添加一个新 fd 到 epoll
	int result = ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
	return result == 0;
}

// 修改某个 fd 的监听事件
bool Epoll::modify(int fd, uint32_t events) {
	if (epoll_fd_ < 0) {
		return false;
	}

	epoll_event event{};
	event.events = events;
	event.data.fd = fd;

	// EPOLL_CTL_MOD：修改已存在 fd 的事件配置
	int result = ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event);
	return result == 0;
}

// 删除某个 fd
bool Epoll::remove(int fd) {
	if (epoll_fd_ < 0) {
		return false;
	}

	// EPOLL_CTL_DEL：把 fd 从 epoll 里移除
	// Linux 下 DEL 时 event 参数通常可传 nullptr
	int result = ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
	return result == 0;
}

// 等待事件
int Epoll::wait(std::vector<epoll_event>& ready_events, int timeout_ms, int max_events) {
	if (epoll_fd_ < 0) {
		return -1;
	}

	// 先把 vector 扩到 max_events，给 epoll_wait 提供可写空间
	ready_events.resize(max_events);

	// epoll_wait 参数说明：
	// 1) epoll_fd_：哪个 epoll 实例
	// 2) ready_events.data()：内核把就绪事件写到这里
	// 3) max_events：最多返回多少个事件
	// 4) timeout_ms：超时等待毫秒
	//
	// 返回值：
	// >0 就绪事件数量
	//  0 超时
	// -1 出错
	int ready_count = ::epoll_wait(epoll_fd_, ready_events.data(), max_events, timeout_ms);

	if (ready_count < 0) {
		return -1;
	}

	// 把 vector 缩到“实际就绪事件个数”，后续遍历更安全直观
	ready_events.resize(ready_count);
	return ready_count;
}

// 关闭 epoll fd
void Epoll::close() {
	if (epoll_fd_ >= 0) {
		::close(epoll_fd_);
		epoll_fd_ = -1;
	}
}