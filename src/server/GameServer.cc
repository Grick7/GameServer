#include "GameServer.h"

#include <iostream>
#include <memory>
GameServer::GameServer(boost::asio::io_context &io, short port, MessageDispatcher &dispatcher, ThreadPool &pool)
    : acceptor_(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      io_(io),
      dispatcher_(dispatcher),
      worker_pool_(pool)
{
  start_accept();
}

// 开始监听连接
void GameServer::start_accept()
{
  // 关键修改点 1: 在创建 Session 时，将 worker_pool_ 引用传递进去
  auto session = std::make_shared<Session>(
      io_,
      dispatcher_,
      worker_pool_ // ✅ 注入 ThreadPool 引用
  );

  acceptor_.async_accept(session->socket(),
                         [session, this](const boost::system::error_code &ec)
                         {
                           if (!ec)
                           {
                             // 启动读
                             session->start();
                           }
                           // 关键修改点 2: 无论成功失败，都继续监听
                           start_accept();
                         });
}