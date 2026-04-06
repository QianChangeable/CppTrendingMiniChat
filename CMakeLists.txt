cmake_minimum_required(VERSION 3.10)
project(chatroom CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# 查找第三方库
# 你已经安装过：
# - spdlog（源码 make install）
# - nlohmann-json3-dev（apt）
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)

add_library(common
    src/common/socket.cpp
    src/common/epoll.cpp
)

add_executable(server src/server/server.cpp)
target_link_libraries(server
    common
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

add_executable(client src/client/client.cpp)
target_link_libraries(client
    common
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)