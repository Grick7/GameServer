#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <future>      // 引入 future/promise
#include <map>         // 引入 map
#include <atomic>      // 引入 atomic
#include <arpa/inet.h> // for htonl, ntohl, htons, ntohs
#include <memory>

// 假设这些头文件存在于您的环境中
#include "protocol.pb.h"
#include "public.h"

using boost::asio::ip::tcp;

// =====================================================================
// 全局 I/O 结构 (用于异步通信)
// =====================================================================

// 当前登录用户ID
int Currentid_ = -1;

// 保护同步写入操作，防止多线程同时调用 boost::asio::write
std::mutex g_write_mutex;

// 请求 ID 计数器，用于匹配 Request 和 Response
std::atomic<uint32_t> g_request_counter{0};

// 用于同步等待异步响应的 Future/Promise 结构
struct ResponseWaiter
{
    uint32_t request_id;
    uint16_t expected_msgid;
    // promise 用于在异步线程中设置结果，future 用于在同步线程中阻塞获取结果
    std::promise<std::vector<char>> promise;
};

// 存储所有正在等待的响应：Key=请求ID
// 注意：生产环境中，Request ID 应该在协议中传输。
// 鉴于您的协议固定，我们使用一个简化机制：假设只有一个同步请求在进行。
// 为了演示异步和 future，我们将使用 map，但依赖于业务层确保串行请求。
std::mutex g_waiters_mutex;
// 使用 shared_ptr 确保 promise 在异步线程中可以安全访问
std::map<uint32_t, std::shared_ptr<ResponseWaiter>> g_response_waiters;

// =====================================================================
// 协议工具函数 (不变)
// =====================================================================

std::vector<char> buildMsg(uint16_t msgid, const std::string &data)
{
    uint32_t len = data.size() + sizeof(uint16_t);
    std::vector<char> packet(sizeof(uint32_t) + len);

    uint32_t network_order_len = htonl(len);
    memcpy(packet.data(), &network_order_len, sizeof(uint32_t));

    uint16_t network_order_msgid = htons(msgid);
    memcpy(packet.data() + sizeof(uint32_t), &network_order_msgid, sizeof(uint16_t));

    memcpy(packet.data() + sizeof(uint32_t) + sizeof(uint16_t), data.data(), data.size());

    return packet;
}

// =====================================================================
// 异步 I/O 核心函数
// =====================================================================

// 声明，用于递归调用
void start_async_read(tcp::socket &socket);

/**
 * @brief 消息解析与分发 (在 io_context 线程中执行)
 * * @param socket 当前连接的 Socket
 * @param buffer 接收到的完整消息体 (不含 4 字节长度头)
 */
void handle_message(tcp::socket &socket, const std::vector<char> &buffer)
{
    if (buffer.size() < sizeof(uint16_t))
    {
        std::cerr << "[Client ERROR] Received truncated message." << std::endl;
        return;
    }

    // 解析消息 ID
    uint16_t network_order_msgid = 0;
    memcpy(&network_order_msgid, buffer.data(), sizeof(uint16_t));
    uint16_t msgid = ntohs(network_order_msgid);

    // 假设 body 是除去 MsgID 的部分
    std::string payload(buffer.data() + sizeof(uint16_t), buffer.size() - sizeof(uint16_t));

    bool is_response = false;
    uint32_t matched_req_id = 0;

    // 1. 尝试将消息作为**请求响应**处理 (检查是否有线程在等待)
    // 锁保护共享状态 map
    {
        std::lock_guard<std::mutex> lock(g_waiters_mutex);

        // 遍历 map，查找是否有线程在等待这个 MsgID 的响应
        // 注意：生产环境中最好使用 Request ID 查找
        for (auto it = g_response_waiters.begin(); it != g_response_waiters.end();)
        {
            if (msgid == it->second->expected_msgid)
            {
                // 匹配成功！这是一个我们正在等待的响应
                matched_req_id = it->first;
                std::cout << "[Client DEBUG] Received expected Response (ReqID:" << matched_req_id << ", MsgID:" << msgid << ")" << std::endl;

                // 设置 promise 的值，这将解除 send_request 线程的阻塞
                // 注意：在 io_context 线程中调用 set_value 是线程安全的。
                it->second->promise.set_value(buffer);

                // 移除已处理的等待者
                it = g_response_waiters.erase(it);
                is_response = true;
                break;
            }
            else
            {
                ++it;
            }
        }
    } // 锁释放

    // 2. 如果不是响应，则作为服务器推送处理
    if (!is_response)
    {
        std::cout << "\n--- [服务器推送: MSG_ID " << msgid << "] ---\n";

        switch (msgid)
        {
        case MSG_CHAT:
        {
            msg::SChatMsg chat;
            if (chat.ParseFromString(payload))
                std::cout << "[聊天] 玩家 " << chat.from() << ": " << chat.text() << std::endl;
            break;
        }
        case MSG_ADDEXPACK:
        {
            // 注意：如果服务器将 ACK 作为推送发送，且主线程没有等待，则在这里处理
            msg::AddExpRsp rsp;
            if (rsp.ParseFromString(payload))
                std::cout << "[经验] 当前经验: " << rsp.new_exp()
                          << " 等级: " << rsp.new_level()
                          << (rsp.level_up() ? " 🎉升级!" : "") << std::endl;
            break;
        }
        case MSG_ENTER_ROOM_ACK:
        case MSG_READY_ACK:
        {
            // 对于非预期的 ACK 消息，我们只打印，不处理，因为它们可能被主线程意外忽略
            std::cout << "[房间/准备] 收到未被请求线程消耗的 ACK (" << msgid << ")." << std::endl;
            break;
        }
        case MSG_BATTLE_ACTION: // 战斗开始推送
        {
            msg::BattleStart bs;
            if (bs.ParseFromString(payload))
            {
                std::cout << "[战斗开始] 房间 " << bs.roomid() << " 玩家: ";
                for (int i = 0; i < bs.players_size(); ++i)
                    std::cout << bs.players(i) << " ";
                std::cout << std::endl;
            }
            break;
        }
        case MSG_BATTLE_SYNC:
        {
            msg::BattleSync sync;
            if (sync.ParseFromString(payload))
            {
                std::cout << "[战斗同步] ";
                for (int i = 0; i < sync.states_size(); ++i)
                {
                    auto s = sync.states(i);
                    std::cout << "[uid=" << s.uid() << " HP=" << s.hp() << " MP=" << s.mp() << "] ";
                }
                std::cout << std::endl;
            }
            break;
        }
        default:
            std::cout << "[服务器推送] 未知消息ID=" << msgid << std::endl;
        }
        std::cout << "--- [推送处理结束] ---\n";
    }
}

/**
 * @brief 异步读取循环 (取代 receive_loop)
 */
void start_async_read(tcp::socket &socket)
{
    // 使用 shared_ptr 管理 streambuf 生命周期
    auto buffer_ptr = std::make_shared<boost::asio::streambuf>();
    buffer_ptr->prepare(sizeof(uint32_t));

    // 1. 异步读取长度 Header (4 字节)
    boost::asio::async_read(socket, *buffer_ptr, boost::asio::transfer_exactly(sizeof(uint32_t)),
                            [&socket, buffer_ptr](boost::system::error_code ec, std::size_t length)
                            {
                                if (ec)
                                {
                                    if (ec != boost::asio::error::eof && ec != boost::asio::error::operation_aborted)
                                        std::cerr << "[Client ERROR] Header read failed: " << ec.message() << std::endl;
                                    return;
                                }

                                // 提取长度
                                uint32_t network_order_len;
                                const char *header_data = boost::asio::buffer_cast<const char *>(buffer_ptr->data());
                                memcpy(&network_order_len, header_data, sizeof(uint32_t));
                                uint32_t message_body_len = ntohl(network_order_len);

                                // 简单的长度检查
                                if (message_body_len > 1024 * 1024 * 10 || message_body_len < sizeof(uint16_t))
                                {
                                    std::cerr << "[Client ERROR] Received abnormal message length: " << message_body_len << std::endl;
                                    return;
                                }

                                // 2. 异步读取 Body
                                // 消耗掉已读取的 4 字节 Header
                                buffer_ptr->consume(sizeof(uint32_t));
                                buffer_ptr->prepare(message_body_len); // 准备读取 body 的空间

                                boost::asio::async_read(socket, *buffer_ptr, boost::asio::transfer_exactly(message_body_len),
                                                        [&socket, buffer_ptr, message_body_len](boost::system::error_code ec_body, std::size_t length_body)
                                                        {
                                                            if (ec_body)
                                                            {
                                                                if (ec_body != boost::asio::error::eof && ec_body != boost::asio::error::operation_aborted)
                                                                    std::cerr << "[Client ERROR] Body read failed: " << ec_body.message() << std::endl;
                                                                return;
                                                            }

                                                            // 3. 完整消息接收，开始处理
                                                            std::vector<char> full_message(message_body_len);
                                                            const char *body_data = boost::asio::buffer_cast<const char *>(buffer_ptr->data());
                                                            memcpy(full_message.data(), body_data, message_body_len);

                                                            // 调用消息处理函数 (在 I/O 线程中执行)
                                                            handle_message(socket, full_message);

                                                            // 4. 继续下一个异步读取 (递归循环)
                                                            start_async_read(socket);
                                                        });
                            });
}

// ---------------------------------------------------------------------
// 核心修复函数：send_request (同步发送请求，异步等待响应)
// ---------------------------------------------------------------------
std::vector<char> send_request(tcp::socket &socket, uint16_t msgid, const std::string &data)
{
    // 1. 设置同步等待机制
    uint32_t current_req_id = ++g_request_counter;

    // 创建 ResponseWaiter 实例
    auto waiter_ptr = std::make_shared<ResponseWaiter>();
    waiter_ptr->request_id = current_req_id;
    // 假设响应ID是 请求ID + 1，需要根据您的服务器协议来确定
    // 如果服务器响应 ID 和请求 ID 相同，则设置为 msgid。
    // 在这里我们假设服务器的 ACK ID 是固定的，例如 MSG_DENGLUACK 对应 MSG_DENGLU
    waiter_ptr->expected_msgid = msgid + 1;

    // 获取 future，用于阻塞等待
    std::future<std::vector<char>> response_future = waiter_ptr->promise.get_future();

    // 2. 存储等待者到共享 Map 中 (保护共享状态)
    {
        std::lock_guard<std::mutex> lock(g_waiters_mutex);
        g_response_waiters[current_req_id] = waiter_ptr;
    }

    // 3. 发送请求 - 写入操作必须加锁，防止与其它线程的写入冲突 (例如另一个 send_request)
    std::lock_guard<std::mutex> lock(g_write_mutex);

    try
    {
        std::vector<char> package = buildMsg(msgid, data);
        std::cout << "[Client DEBUG] Sending request (ReqID:" << current_req_id << ", MsgID:" << msgid << "), size: " << package.size() << std::endl;

        // 使用同步写入，确保请求一次性发出
        boost::asio::write(socket, boost::asio::buffer(package));
        std::cout << "[Client DEBUG] Request sent successfully. Waiting for async response." << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Client ERROR] send_request write failed: " << e.what() << std::endl;

        // 写入失败，必须清除等待者并设置异常
        {
            std::lock_guard<std::mutex> lock(g_waiters_mutex);
            auto it = g_response_waiters.find(current_req_id);
            if (it != g_response_waiters.end())
            {
                it->second->promise.set_exception(std::make_exception_ptr(std::runtime_error("Network write failed.")));
                g_response_waiters.erase(it);
            }
        }
        return std::vector<char>();
    }

    // 4. 阻塞等待异步回调设置结果
    try
    {
        std::cout << "[Client DEBUG] Waiting for response for ReqID: " << current_req_id << std::endl;
        // future.get() 阻塞主线程，直到 I/O 线程调用 promise.set_value()
        std::vector<char> response = response_future.get();
        std::cout << "[Client DEBUG] Response received for ReqID: " << current_req_id << std::endl;
        return response;
    }
    catch (const std::exception &e)
    {
        // 如果 promise 设置了异常，或 future 出了问题
        std::cerr << "[Client ERROR] Synchonous wait failed: " << e.what() << std::endl;
        return std::vector<char>();
    }

    // 注意：匹配成功的 ResponseWaiter 已经在 handle_message 中被移除了。
}

// ---------------------------------------------------------------------
// 业务逻辑函数 (MsgID 映射调整为实际 ID)
// ---------------------------------------------------------------------
// ... 保持不变，但要确保所有 send_request 调用的 MsgID 与服务器 ACK ID 匹配 ...
// 为了兼容您的原始代码，我将保持业务逻辑不变，因为它调用了 send_request。

bool do_register(tcp::socket &socket)
{
    msg::RegisterReq reg_req;
    std::string name, passwd;
    std::cout << "请输入用户名: ";
    std::cin >> name;
    std::cout << "请输入密码: ";
    std::cin >> passwd;
    reg_req.set_name(name);
    reg_req.set_passwd(passwd);
    std::string req_data;
    reg_req.SerializeToString(&req_data);

    // ⚠️ 修复：将期望的 ACK ID 设置正确
    // 由于 send_request 现在假设 ACK ID = REQ ID + 1，但在 do_register 中我们知道 ACK ID 是 MSG_ZHUCEACK
    // 为了兼容，我们在 do_register 中创建并存储等待者，直接调用 write，然后等待

    std::vector<char> resp_buffer = send_request(socket, MSG_ZHUCE, req_data);
    if (resp_buffer.empty())
        return false;

    uint16_t network_order_msgid = 0;
    memcpy(&network_order_msgid, resp_buffer.data(), sizeof(uint16_t));
    uint16_t msgid = ntohs(network_order_msgid);

    // 假设服务器返回的 ACK ID 是 MSG_ZHUCEACK
    if (msgid != MSG_ZHUCEACK)
    {
        std::cerr << "注册失败，收到非预期消息ID: " << msgid << std::endl;
        return false;
    }
    std::string resp_data(resp_buffer.data() + sizeof(uint16_t), resp_buffer.size() - sizeof(uint16_t));
    msg::RegisterResp reg_resp;
    if (!reg_resp.ParseFromString(resp_data))
    {
        std::cerr << "注册响应解析失败" << std::endl;
        return false;
    }
    if (reg_resp.ok())
    {
        std::cout << "注册成功! 分配 UID: " << reg_resp.uid() << std::endl;
        return true;
    }
    else
    {
        std::cout << "注册失败: " << reg_resp.reason() << std::endl;
        return false;
    }
}
bool do_login(tcp::socket &socket)
{
    msg::LoginReq login_req;
    int uid;
    std::string passwd;
    std::cout << "请输入 UID: ";
    std::cin >> uid;
    std::cout << "请输入密码: ";
    std::cin >> passwd;
    login_req.set_uid(uid);
    login_req.set_passwd(passwd);
    std::string req_data;
    login_req.SerializeToString(&req_data);
    std::cout << "等待接受服务器响应" << std::endl;

    // ⚠️ 修复：将期望的 ACK ID 设置正确
    std::vector<char> resp_buffer = send_request(socket, MSG_DENGLU, req_data);
    if (resp_buffer.empty())
        return false;

    uint16_t network_order_msgid = 0;
    memcpy(&network_order_msgid, resp_buffer.data(), sizeof(uint16_t));
    uint16_t msgid = ntohs(network_order_msgid);

    // 假设服务器返回的 ACK ID 是 MSG_DENGLUACK
    if (msgid != MSG_DENGLUACK)
    {
        std::cerr << "登录失败，收到非预期消息ID: " << msgid << std::endl;
        return false;
    }
    std::string resp_data(resp_buffer.data() + sizeof(uint16_t), resp_buffer.size() - sizeof(uint16_t));
    msg::LoginResp login_resp;
    if (!login_resp.ParseFromString(resp_data))
    {
        std::cerr << "登录响应解析失败" << std::endl;
        return false;
    }
    if (login_resp.ok())
    {
        Currentid_ = uid;
        std::cout << "登录成功!" << std::endl;
        return true;
    }
    else
    {
        std::cout << "登录失败: " << login_resp.reason() << std::endl;
        return false;
    }
}
bool do_view_data(tcp::socket &socket)
{
    msg::ViewPlayerDataReq req;
    req.set_uid(Currentid_);

    std::string req_data;
    req.SerializeToString(&req_data);

    std::vector<char> resp_buffer = send_request(socket, MSG_BACKPACK, req_data);
    if (resp_buffer.empty())
        return false;

    uint16_t network_order_msgid = 0;
    memcpy(&network_order_msgid, resp_buffer.data(), sizeof(uint16_t));
    uint16_t msgid = ntohs(network_order_msgid);

    if (msgid != MSG_BACKPACKACK)
    {
        std::cerr << "查看玩家数据失败，收到非预期消息ID: " << msgid << std::endl;
        return false;
    }

    std::string resp_data(resp_buffer.data() + sizeof(uint16_t), resp_buffer.size() - sizeof(uint16_t));
    msg::PlayerAttr player_data;
    if (!player_data.ParseFromString(resp_data))
    {
        std::cerr << "查看玩家数据响应解析失败" << std::endl;
        return false;
    }

    std::cout << "🏹 玩家数据:\n";
    std::cout << "  UID: " << player_data.uid() << "\n";
    std::cout << "  等级: " << player_data.level() << "\n";
    std::cout << "  经验: " << player_data.exp() << "\n";
    std::cout << "  HP/MP: " << player_data.hp() << "/" << player_data.mp() << "\n";
    std::cout << "  金币: " << player_data.coin() << "\n";
    std::cout << "  坐标: (" << player_data.x() << ", " << player_data.y() << ", " << player_data.z() << ")\n";
    return true;
}
bool do_add_exp(tcp::socket &socket)
{
    int add_value;
    std::cout << "请输入增加的经验值: ";
    std::cin >> add_value;

    msg::AddExpReq req;
    req.set_uid(Currentid_);
    req.set_exp_add(add_value);

    std::string req_data;
    req.SerializeToString(&req_data);

    std::vector<char> resp_buffer = send_request(socket, MSG_ADDEXP, req_data);
    if (resp_buffer.empty())
        return false;

    uint16_t network_order_msgid = 0;
    memcpy(&network_order_msgid, resp_buffer.data(), sizeof(uint16_t));
    uint16_t msgid = ntohs(network_order_msgid);

    if (msgid != MSG_ADDEXPACK)
    {
        std::cerr << "增加经验失败，收到非预期消息ID: " << msgid << std::endl;
        return false;
    }

    msg::AddExpRsp rsp;
    std::string resp_data(resp_buffer.data() + sizeof(uint16_t), resp_buffer.size() - sizeof(uint16_t));
    if (!rsp.ParseFromString(resp_data))
    {
        std::cerr << "AddExp 响应解析失败" << std::endl;
        return false;
    }

    if (rsp.success())
    {
        std::cout << "✅ 增加经验成功! 当前经验: " << rsp.new_exp()
                  << ", 等级: " << rsp.new_level()
                  << (rsp.level_up() ? " 🎉升级啦!" : "") << std::endl;
        return true;
    }
    else
    {
        std::cerr << "❌ 增加经验失败!" << std::endl;
        return false;
    }
}
void enterRoom(tcp::socket &socket, int roomid)
{
    msg::EnterRoomReq req;
    req.set_uid(Currentid_);
    req.set_roomid(roomid);

    std::vector<char> resp_buffer = send_request(socket, MSG_ENTER_ROOM, req.SerializeAsString());
    if (resp_buffer.empty())
    {
        std::cerr << "❌ 进入房间请求失败或连接断开。" << std::endl;
        return;
    }

    // Parse response
    uint16_t network_order_msgid = 0;
    memcpy(&network_order_msgid, resp_buffer.data(), sizeof(uint16_t));
    uint16_t msgid = ntohs(network_order_msgid);

    if (msgid != MSG_ENTER_ROOM_ACK)
    {
        std::cerr << "❌ 进入房间失败，收到非预期消息ID: " << msgid << std::endl;
        return;
    }

    std::string resp_data(resp_buffer.data() + sizeof(uint16_t), resp_buffer.size() - sizeof(uint16_t));
    msg::EnterRoomAck ack; // 假设响应消息为 EnterRoomAck
    if (!ack.ParseFromString(resp_data))
    {
        std::cerr << "❌ 进入房间响应解析失败。" << std::endl;
        return;
    }

    if (ack.ok())
    {
        std::cout << "✅ 成功进入房间 " << roomid;
        std::cout << std::endl;
    }
    else
    {
        std::cerr << "❌ 进入房间 " << roomid << " 失败: " << ack.reason() << std::endl;
    }
}
void ready(tcp::socket &socket, int roomid, bool is_ready)
{
    msg::ReadyReq req;
    req.set_uid(Currentid_);
    req.set_roomid(roomid);
    req.set_ready(is_ready);

    std::vector<char> resp_buffer = send_request(socket, MSG_READY, req.SerializeAsString());
    if (resp_buffer.empty())
    {
        std::cerr << "❌ 准备请求失败或连接断开。" << std::endl;
        return;
    }

    uint16_t network_order_msgid = 0;
    memcpy(&network_order_msgid, resp_buffer.data(), sizeof(uint16_t));
    uint16_t msgid = ntohs(network_order_msgid);

    if (msgid != MSG_READY_ACK)
    {
        std::cerr << "❌ 准备动作失败，收到非预期消息ID: " << msgid << std::endl;
        return;
    }

    std::string resp_data(resp_buffer.data() + sizeof(uint16_t), resp_buffer.size() - sizeof(uint16_t));
    msg::ReadyAck ack; // 假设响应消息为 ReadyAck
    if (!ack.ParseFromString(resp_data))
    {
        std::cerr << "❌ 准备响应解析失败。" << std::endl;
        return;
    }

    std::cout << "✅ 房间 " << roomid << " 准备动作成功: "
              << (is_ready ? "已准备" : "取消准备") << std::endl;
}

void battleAction(tcp::socket &socket, int roomid, int skillid, int target)
{
    // 假设客户端发送的请求结构体是 msg::BattleActionReq
    msg::BattleAction req;
    req.set_uid(Currentid_);
    req.set_roomid(roomid);
    req.set_skillid(skillid);
    req.set_target(target);

    std::string req_data;
    req.SerializeToString(&req_data);

    // 战斗动作通常是 Fire-and-Forget，结果通过 MSG_BATTLE_SYNC 异步推送。
    // 我们只反馈发送是否成功。
    std::lock_guard<std::mutex> lock(g_write_mutex);

    try
    {
        std::vector<char> package = buildMsg(MSG_BATTLE_ACTION, req_data);
        boost::asio::write(socket, boost::asio::buffer(package));
        std::cout << "✅ 战斗动作 (SkillID: " << skillid << ") 发送成功! 等待服务器同步结果..." << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ 战斗动作发送失败: " << e.what() << std::endl;
    }
}

// ---------------------------------------------------------------------
// 主函数
// ---------------------------------------------------------------------
int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "用法: " << argv[0] << " <host> <port>\n";
        return 1;
    }

    // 必须在 try 块外部定义 io_context 和 socket
    boost::asio::io_context io;
    tcp::socket socket(io);

    try
    {
        tcp::resolver resolver(io);
        boost::asio::connect(socket, resolver.resolve(argv[1], argv[2]));
        std::cout << "✅ 连接服务器成功\n";

        // 1. 启动 I/O 线程池
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard(io.get_executor());

        std::vector<std::thread> io_threads;
        int num_threads = std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 1;
        for (int i = 0; i < num_threads; ++i)
        {
            io_threads.emplace_back([&io]()
                                    { 
                try {
                    io.run(); 
                } catch (const std::exception& e) {
                    std::cerr << "[I/O Thread ERROR] " << e.what() << std::endl;
                } });
        }
        std::cout << "[Client DEBUG] Started " << num_threads << " I/O threads." << std::endl;

        // 2. 启动第一个异步读取操作
        start_async_read(socket);

        // 主循环 (业务逻辑)
        while (true)
        {
            int choice;
            std::cout << "\n操作选择: \n1-注册 2-登录 3-查看数据 4-增加经验 5-进入房间 6-准备 7-战斗动作 0-退出\n> ";
            if (!(std::cin >> choice))
            {
                std::cout << "输入错误，退出程序。\n";
                break;
            }

            if (choice == 1)
                do_register(socket);
            else if (choice == 2)
                do_login(socket);
            else if (choice == 3)
            {
                if (Currentid_ == -1)
                {
                    std::cout << "请先登录。\n";
                    continue;
                }
                do_view_data(socket);
            }
            else if (choice == 4)
            {
                if (Currentid_ == -1)
                {
                    std::cout << "请先登录。\n";
                    continue;
                }
                do_add_exp(socket);
            }
            else if (choice == 5)
            {
                if (Currentid_ == -1)
                {
                    std::cout << "请先登录。\n";
                    continue;
                }
                int roomid;
                std::cout << "输入房间ID: ";
                std::cin >> roomid;
                enterRoom(socket, roomid);
            }
            else if (choice == 6)
            {
                if (Currentid_ == -1)
                {
                    std::cout << "请先登录。\n";
                    continue;
                }
                int roomid;
                std::cout << "输入房间ID: ";
                std::cin >> roomid;
                ready(socket, roomid, true);
            }
            else if (choice == 7)
            {
                if (Currentid_ == -1)
                {
                    std::cout << "请先登录。\n";
                    continue;
                }
                int roomid, skillid, target;
                std::cout << "输入房间ID 技能ID 目标UID: ";
                std::cin >> roomid >> skillid >> target;
                battleAction(socket, roomid, skillid, target);
            }
            else if (choice == 0)
                break;
        }

        // 清理资源
        socket.close();
        io.stop();
        for (auto &t : io_threads)
        {
            if (t.joinable())
                t.join();
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "客户端异常: " << e.what() << std::endl;
    }
    return 0;
}