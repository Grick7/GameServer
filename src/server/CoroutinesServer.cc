#include <CoroutinesServer.h>

CoroutinesServer::CoroutinesServer(boost::asio::io_context &io_context, unsigned short port)
    : io_context_(io_context),
      acceptor_(io_context, {boost::asio::ip::tcp::v4(), port})
{
    std::cout << "[Server] Initialized on port " << port << std::endl;

    // 使用 co_spawn 启动协程 listen_for_connections()
    boost::asio::co_spawn(io_context_, [this]
                          { return listen_for_connections(); }, boost::asio::detached);
}
// 核心协程函数：持续监听和接受连接
boost::asio::awaitable<void> CoroutinesServer::listen_for_connections()
{
    try
    {
        for (;;)
        {
            // 创建一个新的socket用于接收连接
            boost::asio::ip::tcp::socket new_socket(io_context_);

            std::cout << "[Server] Waiting for connection..." << std::endl;

            // co_await 异步接受连接
            co_await acceptor_.async_accept(new_socket, boost::asio::use_awaitable);

            std::cout << "[Server] Accepted new connection." << std::endl;

            // 接受连接后，创建一个新的 Session 对象并启动它
            std::make_shared<CoroutinesSession>(std::move(new_socket))->start();

            // 循环会自动继续，等待下一个连接
        }
    }
    catch (const boost::system::system_error &e)
    {
        // 集中处理Acceptor的错误（例如，服务器关闭）
        if (e.code() != boost::asio::error::operation_aborted)
        {
            std::cerr << "[Server] Acceptor Error: " << e.what() << std::endl;
        }
    }
}