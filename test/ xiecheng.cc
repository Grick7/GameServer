
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <iostream>
class xiechengServer
{
public:
    xiechengServer(boost::asio::io_context &io_context, unsigned short port)
        : io_(io_context), acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
    {
        boost::asio::co_spawn(io_, [this]
                              { return accept(); }, boost::asio::detached);
    }

private:
    boost::asio::ip::tcp::acceptor acceptor_; // 用于接受连接请求
    boost::asio::io_context &io_;
    boost::asio::awaitable<void> accept()
    {
        try
        {
            for (;;)
            {
                // 不断监听新连接
                boost::asio::ip::tcp::socket new_socket(io_);
                // co_await表示异步接受连接 这是一个协程函数，会将监听挂起，释放线程，有新链接后继续调用。
                co_await acceptor_.async_accept(new_socket, boost::asio::use_awaitable);
                // 创建新会话，继续监听连接
            }
        }
        catch (boost::system::system_error &er)
        {
            std::cerr << "[Server] Unexpected std::exception: " << er.what() << std::endl;
        }
    }
};