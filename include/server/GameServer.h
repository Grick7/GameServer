#pragma once
#include "Session.h"
#include "ThreadPool.h"
#include <boost/asio.hpp>

class GameServer
{
public:
  GameServer(boost::asio::io_context &io, short port, MessageDispatcher &dispatcher, ThreadPool &pool);

  // 开始监听连接
  void start_accept();

private:
  boost::asio::io_context &io_;
  boost::asio::ip::tcp::acceptor acceptor_;
  MessageDispatcher &dispatcher_;
  ThreadPool &worker_pool_; // ✅ GameServer 也要持有 ThreadPool 引用
};