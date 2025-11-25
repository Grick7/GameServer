#include <CoroutinesSession.h>

// 构造函数：接受一个已连接的socket
CoroutinesSession::CoroutinesSession(boost::asio::ip::tcp::socket socket)
    : socket_(std::move(socket))
{
    static size_t next_id = 1;
    id_ = next_id++;
    std::cout << "[Session " << id_ << "] Created." << std::endl;
}
// 核心协程函数：处理连接的生命周期和所有I/O
boost::asio::awaitable<void> CoroutinesSession::run()
{
    try
    {
        readbuffer_.resize(1024); // 初始化读取缓冲区大小
        std::cout << "[Session " << id_ << "] Running." << std::endl;

        // 1. 读循环
        for (;;)
        {
            // co_await 异步读取：看起来像同步调用
            std::size_t bytes_read = co_await socket_.async_read_some(
                boost::asio::buffer(readbuffer_),
                boost::asio::use_awaitable);

            std::cout << "[Session " << id_ << "] Read " << bytes_read << " bytes." << std::endl;

            // 2. 假设我们将收到的数据回显回去 (Echo Server)
            // co_await 异步写入
            co_await boost::asio::async_write(
                socket_,
                boost::asio::buffer(readbuffer_.data(), bytes_read),
                boost::asio::use_awaitable);

            std::cout << "[Session " << id_ << "] Wrote " << bytes_read << " bytes (Echo)." << std::endl;

            // 3. 继续循环读取
        }
    }
    catch (const boost::system::system_error &e)
    {
        // 集中处理所有错误，例如连接关闭 (EOF) 或其他网络错误
        if (e.code() != boost::asio::error::eof &&
            e.code() != boost::asio::error::operation_aborted)
        {
            std::cerr << "[Session " << id_ << "] Error: " << e.what() << std::endl;
        }
    }

    // 4. 清理：当协程结束（无论是成功还是异常），自动关闭连接
    std::cout << "[Session " << id_ << "] Closing." << std::endl;
    socket_.close();
}

// 启动会话：通过 co_spawn 启动主要协程
void CoroutinesSession::start()
{
    // 使用 co_spawn 启动协程 run()，并使用 detached 策略
    // detached 意味着我们不关心 run() 协程的返回值或异常（它会通过 try-catch 处理）
    boost::asio::co_spawn(socket_.get_executor(), [self = shared_from_this()]
                          { return self->run(); }, boost::asio::detached);
}