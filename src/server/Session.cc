#include "Session.h"
#include "protocol.pb.h"
#include "MessageDispatcher.h"
#include "SessionManager.h"

#include <memory>

// 静态成员的定义和初始化
// 从 1 或 0 开始计数，这里从 1 开始
std::atomic<int> Session::next_id_{1};

// 构造函数定义 (实现)
Session::Session(boost::asio::io_context &io,
                 MessageDispatcher &dispatch,
                 ThreadPool &worker_pool)
    // --- 初始化列表 ---
    : socket_(io),
      strand_(io.get_executor()),
      timer_(io),
      hearttime_(std::chrono::seconds(100)), // 假设 10s 可用
      heartnum_(0),
      id_(next_id_++),
      uid_(-1),
      dispatcher_(dispatch),
      worker_pool_(worker_pool) // 正确初始化
{
  // 构造函数体
}
boost::asio::ip::tcp::socket &Session::socket()
{
  return socket_;
}
void Session::start()
{
  // 1. 设置 TCP_NODELAY 选项
  boost::asio::ip::tcp::no_delay option(true);
  boost::system::error_code ec;

  // 假设 socket_ 是 Session 类的成员变量
  socket_.set_option(option, ec);

  if (ec)
  {
    // 建议打印错误日志，但在大多数情况下可以忽略
    // std::cerr << "Failed to set TCP_NODELAY: " << ec.message() << std::endl;
  }

  // 2. 注册会话到管理器
  SessionManager::getinstance().add(id_, shared_from_this());

  // 3. 启动异步读取
  do_read();

  // 4. 启动心跳机制
  heart_beat();
}

void Session::heart_beat() // 心跳计数
{
  timer_.expires_after(hearttime_);
  auto self = shared_from_this();

  timer_.async_wait(boost::asio::bind_executor(strand_, [self, this](const boost::system::error_code &ec)
                                               {
    if (!ec)
    {
      heartnum_++;
      if (heartnum_ > 2)
      {
        std::cout << "长时间未进行通信，断开连接" << std::endl;
        close();
        return;
      }
    }
    heart_beat(); }));
}

void Session::do_read()
{
  std::cout << "[Server DEBUG] STARTING async_read for Session ID " << id_ << std::endl;
  auto self = shared_from_this();

  socket_.async_read_some(boost::asio::buffer(readbuffer_), boost::asio::bind_executor(strand_, [self, this](const boost::system::error_code &ec, std::size_t len)
                                                                                       {
                                                               if (!ec)
                                                               {
                                                                  heartnum_=0;
                                                                   buffer_.insert(buffer_.end(), readbuffer_.data(), readbuffer_.data() + len);
                                                                   // 验证信息完整性
                                                                   get_message();
                                                                   // 继续读数据
                                                                   do_read();
                                                               }
                                                               else
                                                               {
                                                                   //std::cout << "读取数据失败 关闭连接" << std::endl;
                                                                   close();
                                                               } }));
}

void Session::send(const std::vector<char> &data)
{
  auto self = shared_from_this();
  boost::asio::post(strand_, [this, self, data = std::move(data)]() mutable
                    {
        bool start_write = write_queue_.empty(); // 检查队列在添加数据前是否为空
        
        write_queue_.push_back(std::move(data));
        
        if (start_write) // 如果队列之前为空，则启动写入链
        {
            // 关键日志：确认调度成功
            std::cout << "[Server DEBUG] SCHEDULING do_write for Session ID " << id_ << std::endl; 
            do_write(); 
        } });
}

void Session::do_write()
{
  std::cout << "[Server DEBUG] STARTING async_write for Session ID " << id_ << std::endl; // 👈 关键日志 B
  auto self = shared_from_this();
  boost::asio::async_write(socket_,
                           boost::asio::buffer(write_queue_.front()),
                           boost::asio::bind_executor(strand_,
                                                      [this, self](boost::system::error_code ec, std::size_t)
                                                      {
                                                        // 关键日志 C: 检查 async_write 是否完成，以及是否出错
                                                        std::cout << "[Server DEBUG] async_write completed for Session ID " << id_
                                                                  << ", Error: " << ec.message() << std::endl;
                                                        if (!ec)
                                                        {
                                                          write_queue_.pop_front();
                                                          if (!write_queue_.empty())
                                                            do_write();
                                                        }
                                                        else
                                                        {
                                                          close();
                                                        }
                                                      }));
}

void Session::get_message() // msglen(4) + msgid(2) + msg
{
  // 确保包含必要的头文件，例如：#include <arpa/inet.h> 或 #include <netinet/in.h>

  // 6 是最小的 Header 长度 (4 字节长度 + 2 字节 MsgID)
  while (buffer_.size() >= 6)
  {
    // ------------------------------------------------------------------
    // 1. 读取总长度 (4 字节) 并处理字节序
    // ------------------------------------------------------------------
    uint32_t network_order_len = 0;
    // 原始数据包的前 4 字节是网络字节序的长度
    memcpy(&network_order_len, buffer_.data(), sizeof(uint32_t));

    // 关键修复 1: 将网络字节序转换回本机字节序
    uint32_t total_len = ntohl(network_order_len); // msglen = msgid + 数据长度
                                                   // !!! 关键日志 !!!
    std::cout << "[Server] Checking Message. Buffer size: " << buffer_.size()
              << ", Expected total_len: " << total_len << std::endl;
    // 安全检查：如果长度值异常大，可能是协议错误
    if (total_len > 1024 * 1024 * 10 || total_len < sizeof(uint16_t))
    {
      // 收到异常长度，通常表示连接或协议错误。
      // 建议关闭连接并记录错误。
      // 这里简单返回，但在实际生产环境中需要更严格的处理
      std::cerr << "Protocol Error: Received abnormal message length: " << total_len << std::endl;
      // 退出解析，可能需要关闭 socket
      return;
    }

    // 检查数据包是否完整：[4 字节 Header] + [total_len 字节 Body]
    if (buffer_.size() < sizeof(uint32_t) + total_len)
      return; // 数据不完整，等待更多数据

    // ------------------------------------------------------------------
    // 2. 读取消息 ID (2 字节) 并处理字节序
    // ------------------------------------------------------------------
    uint16_t network_order_msgid = 0;
    // 消息 ID 位于总长度之后，即偏移 4 字节处
    memcpy(&network_order_msgid, buffer_.data() + sizeof(uint32_t), sizeof(uint16_t));

    // 关键修复 2: 将网络字节序转换回本机字节序
    uint16_t msgid = ntohs(network_order_msgid); // 前2字节是 msgid

    // ------------------------------------------------------------------
    // 3. 提取 Protobuf 数据 (Body)
    // ------------------------------------------------------------------
    // 数据长度 = 总 Body 长度 - MsgID 长度
    size_t body_len = total_len - sizeof(uint16_t);

    // 消息的开始位置是 4 字节长度 + 2 字节 MsgID = 6 字节
    // 提取 Protobuf 数据部分
    std::vector<char> msg(
        buffer_.begin() + sizeof(uint32_t) + sizeof(uint16_t),
        buffer_.begin() + sizeof(uint32_t) + sizeof(uint16_t) + body_len);

    // ------------------------------------------------------------------
    // 4. 处理消息
    // ------------------------------------------------------------------
    handle_message(msgid, msg);

    // 清除已经处理的数据
    // 清除长度 Header (4 字节) 和整个 Body (total_len 字节)
    buffer_.erase(buffer_.begin(), buffer_.begin() + sizeof(uint32_t) + total_len);
  }
}
void Session::handle_message(uint16_t msgid, std::vector<char> &msg)
{
  // 把信息提交给信息处理框架进行处理
  std::string body(msg.begin(), msg.end());
  dispatcher_.Dispatch(id_, msgid, body);
}

void Session::close()
{
  std::cout << "[Server DEBUG] Closing session " << id_ << "..." << std::endl;
  boost::system::error_code ec;
  socket_.close(ec);

  auto uid_copy = uid_;
  auto id_copy = id_;
  // ✅ 直接使用成员 worker_pool_ 来投递任务
  worker_pool_.enqueue([uid_copy, id_copy]()
                       { 
        SessionManager::getinstance().RemoveUser(uid_copy);
        SessionManager::getinstance().del(id_copy); });

  std::cout << "[Server DEBUG] Session closed." << std::endl;
}