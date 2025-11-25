#pragma once
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <iostream>
#include <CoroutinesSession.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/detached.hpp>
// --- Server 类：负责监听和接受连接 ---
class CoroutinesServer
{
public:
    CoroutinesServer(boost::asio::io_context &io_context, unsigned short port);

private:
    boost::asio::io_context &io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;

    // 核心协程函数：持续监听和接受连接
    boost::asio::awaitable<void> listen_for_connections();
};