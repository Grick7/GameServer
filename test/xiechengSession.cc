#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <iostream>
#include <array>
class xiechengSession : public std::enable_shared_from_this<xiechengSession>
{

public:
    xiechengSession(boost::asio::ip::tcp::socket &socket) : socket_(std::move(socket))
    {
    }
    void start()
    {
        boost::asio::co_spawn(socket_.get_executor(), [self = shared_from_this()]
                              { return self->do_read(); }, boost::asio::detached);
    }
    void close_socket()
    {
        if (socket_.is_open())
        {
            socket_.close();
        }
    }
    boost::asio::awaitable<void> do_read()
    {
        try
        {
            for (;;)
            {

                size_t bytes_transferred = co_await socket_.async_read_some(boost::asio::buffer(read_buffer_), boost::asio::use_awaitable);
                if (bytes_transferred == 0)
                {
                    std::cout << "[Session] Connection closed by peer." << std::endl;
                    break; // 退出循环，协程结束
                }
                handle_message(bytes_transferred);
            }
        }
        catch (boost::system::system_error &er)
        {
            // 捕获连接相关的错误
            if (er.code() != boost::asio::error::operation_aborted) // 如果不是主动关闭连接 则检测错误信息
            {
                std::cerr << "[Session] Read Error ("
                          << socket_.remote_endpoint().address().to_string()
                          << "): " << er.what() << std::endl;
            }
        }
        close_socket(); // 关闭连接
    }
    boost::asio::awaitable<void> do_write(std::string_view message)
    {
        try
        {
            co_await boost::asio::async_write(
                socket_,
                boost::asio::buffer(message.data(), message.size()),
                boost::asio::use_awaitable);
        }
        catch (boost::system::system_error &e)
        {
            // 捕获 Asio 抛出的异常

            // 1. 忽略操作取消错误（operation_aborted）
            if (e.code() == boost::asio::error::operation_aborted)
            {
                // 这是正常的主动取消或 socket 关闭信号，直接退出
                std::cout << "[Session] Write operation aborted." << std::endl;
            }
            // 2. 捕获连接错误
            else if (e.code() == boost::asio::error::broken_pipe ||
                     e.code() == boost::asio::error::connection_reset)
            {
                // 连接中断、管道破裂等，通常意味着客户端已断开
                std::cerr << "[Session] Write failed: Connection error ("
                          << e.what() << "). Closing socket." << std::endl;
                // TODO: 在这里调用您的 close_socket/disconnect 逻辑
            }
            // 3. 捕获其他未知错误
            else
            {
                std::cerr << "[Session] Unknown Write Error: " << e.what() << std::endl;
            }
        }
    }

    // --- 辅助方法 ---
    void handle_message(size_t bytes_transferred)
    {
        std::string_view msg_view(read_buffer_.data(), bytes_transferred);
        std::cout << "[Session] Received: " << msg_view << std::endl;

        // 模拟业务响应：将收到的消息回写（Echo）
        boost::asio::co_spawn(socket_.get_executor(),
                              do_write(msg_view),
                              boost::asio::detached);
    }

private:
    boost::asio::ip::tcp::socket socket_;
    // 读缓冲区
    std::array<char, 1024> read_buffer_;
};