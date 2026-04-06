# CppTrendingMiniChat

一个基于 C++17 的局域网聊天室，支持多客户端并发通信，采用 epoll + 非阻塞 IO，支持 JSON 消息格式，适合学习和实践网络编程。

## 功能简介

- 支持多客户端同时在线
- 消息自动广播
- JSON 协议，易于扩展
- 代码结构清晰，易于理解

## 构建方法

本项目使用 CMake 构建，依赖如下：

- CMake ≥ 3.10
- g++ ≥ 7，支持 C++17
- [spdlog](https://github.com/gabime/spdlog) 日志库
- [nlohmann/json](https://github.com/nlohmann/json) JSON 库

### 安装依赖（Ubuntu）

```sh
sudo apt update
sudo apt install g++ cmake nlohmann-json3-dev
# spdlog 推荐源码安装
git clone https://github.com/gabime/spdlog.git
cd spdlog && mkdir build && cd build
cmake .. && make -j && sudo make install
```

### 编译项目

在项目根目录下执行：

```sh
cmake -S . -B build
cmake --build build -j
```

编译完成后，build 目录下会生成 `server` 和 `client` 可执行文件。

## 运行方法

### 启动服务端

```sh
./build/server
```

服务端会监听 8888 端口，等待客户端连接。

### 启动客户端

在另一终端运行：

```sh
./build/client
```

- 输入消息并回车即可发送
- 输入 `quit` 并回车可安全退出

## 目录结构

```
.
├── include/         # 头文件
├── src/             # 源码
│   ├── client/      # 客户端
│   ├── common/      # 公共模块
│   └── server/      # 服务端
├── CMakeLists.txt   # CMake 构建脚本
├── README.md
└── .gitignore
```

## 其他说明

- 默认监听本机所有网卡（0.0.0.0:8888）
- 支持多客户端并发
- 仅用于学习和交流
