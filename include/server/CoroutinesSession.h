#pragma once
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <iostream>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/co_spawn.hpp>
// --- Session 类：负责单个连接的读写 ---
class CoroutinesSession : public std::enable_shared_from_this<CoroutinesSession>
{
public:
    // 构造函数：接受一个已连接的socket
    explicit CoroutinesSession(boost::asio::ip::tcp::socket socket);

    // 启动会话：通过 co_spawn 启动主要协程
    void start();


private:
    boost::asio::ip::tcp::socket socket_;
    size_t id_;
    std::vector<char> readbuffer_; // 缓冲区用于读取数据

    // 核心协程函数：处理连接的生命周期和所有I/O
    boost::asio::awaitable<void> run();
};