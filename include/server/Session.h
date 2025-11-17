#pragma once
#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <deque>
#include <atomic>

#include "ThreadPool.h"
using namespace std::chrono_literals;
class MessageDispatcher;
class Session : public std::enable_shared_from_this<Session>
{
public:
  Session(boost::asio::io_context &io, MessageDispatcher &dispatch, ThreadPool &worker_pool);

  boost::asio::ip::tcp::socket &socket();

  void reset_heartbeat()
  {
    heartnum_ = 0;
  }
  void start();

  void heart_beat();

  void close();

  void do_read(); // 读操作

  void do_write(); // 写操作

  void get_message(); // 获取一条完整信息

  void handle_message(uint16_t msgid, std::vector<char> &msg); // 处理信息

  void send(const std::vector<char> &data); // 将要发送的信息提交到写队列

  int getid()
  {
    return id_;
  }

private:
  boost::asio::ip::tcp::socket socket_;

  boost::asio::strand<boost::asio::io_context::executor_type> strand_; // 防止同一socket同时读写

  boost::asio::steady_timer timer_; // 心跳检测

  std::chrono::seconds hearttime_; // 心跳检测时间

  int heartnum_; // 心跳次数

  std::array<char, 1024> readbuffer_; // 用于接受信息
  std::vector<char> buffer_;          // 用于拆包粘包

  std::deque<std::vector<char>> write_queue_; // 写队列 用于写数据

  int id_;  // 用于通信区分不同session
  int uid_; // 用户id
  // **新增静态成员：用于生成唯一 ID 的计数器**
  static std::atomic<int> next_id_;
  MessageDispatcher &dispatcher_; // 用于分发任务
  ThreadPool &worker_pool_;       // ✅ 新增：对工作线程池的引用
};