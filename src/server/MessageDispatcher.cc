#include "MessageDispatcher.h"
#include "PlayerDataManager.h"
#include "ThreadPool.h"
#include "RoomManager.h"
#include "Room.h"
// ✅ 私有构造函数，自动注册 handler
MessageDispatcher::MessageDispatcher(ThreadPool &pool) : pool_(pool)
{
    Register(MSG_CHAT, std::bind(&MessageDispatcher::Chat_handle, this,
                                 std::placeholders::_1, std::placeholders::_2));
    Register(MSG_ZHUCE, std::bind(&MessageDispatcher::Zhuce_handle, this,
                                  std::placeholders::_1, std::placeholders::_2));
    Register(MSG_DENGLU, std::bind(&MessageDispatcher::Denglu_handle, this,
                                   std::placeholders::_1, std::placeholders::_2));
    Register(MSG_BACKPACK, std::bind(&MessageDispatcher::Backpack_handle, this,
                                     std::placeholders::_1, std::placeholders::_2));
    Register(MSG_ADDEXP, std::bind(&MessageDispatcher::AddExp_handle, this,
                                   std::placeholders::_1, std::placeholders::_2));
    Register(MSG_ENTER_ROOM, std::bind(&MessageDispatcher::EnterRoom_handle, this,
                                       std::placeholders::_1, std::placeholders::_2));
    Register(MSG_READY, std::bind(&MessageDispatcher::Ready_handle, this,
                                  std::placeholders::_1, std::placeholders::_2));
    Register(MSG_BATTLE_ACTION, std::bind(&MessageDispatcher::BattleAction_handle, this,
                                          std::placeholders::_1, std::placeholders::_2));
}

MessageDispatcher &MessageDispatcher::instance(ThreadPool &pool)
{
    static MessageDispatcher inst(pool); // C++11保证线程安全
    return inst;
}

// 注册(外部可以手动注册)
void MessageDispatcher::Register(int msg_id, MsgHandler handler)
{
    handlers_[msg_id] = handler;
}

// 分发消息
void MessageDispatcher::Dispatch(int sessionid, int msg_id, const std::string &data)
{
    auto session = SessionManager::getinstance().getSession(sessionid);
    if (session)
    {
        session->reset_heartbeat(); // <--- 关键修复点
    }
    auto it = handlers_.find(msg_id);
    if (it != handlers_.end())
    {
        MsgHandler handler = it->second;
        pool_.enqueue_with_key((long long)sessionid, [handler, sessionid, data]()
                               { handler(sessionid, data); });
    }
    else
    {
        std::cout << "未注册的消息 msg_id = " << msg_id << std::endl;
    }
}

// 聊天消息处理函数
void MessageDispatcher::Chat_handle(int sessionid, const std::string &data)
{
    msg::ChatMsg chatmsg;
    chatmsg.ParseFromString(data);
    auto channel = chatmsg.channel();
    int to = chatmsg.to();
    msg::SChatMsg schatmsg;
    schatmsg.set_from(sessionid);
    schatmsg.set_text(chatmsg.text());
    schatmsg.set_channel(channel);

    std::string send_data;
    schatmsg.SerializeToString(&send_data);
    switch (channel)
    {
    case msg::WORLD:
        // 广播给所有在线用户
        SessionManager::getinstance().guangbo(MSG_CHAT, send_data);
        break;

    case msg::PRIVATE:
        // 私聊
        std::shared_ptr<Session> target_session = SessionManager::getinstance().getOnlineUser(to);
        if (target_session)
        {
            target_session->send(SessionManager::getinstance().buildMsg(MSG_CHAT, send_data));
        }
        break;
    }
}

// 注册消息处理函数
void MessageDispatcher::Zhuce_handle(int sessionid, const std::string &data)
{
    msg::RegisterReq regmsg;
    regmsg.ParseFromString(data);
    GameUser user;
    std::string name = regmsg.name();
    std::string passward = regmsg.passwd();
    user.setname(name);
    user.setpaswd(passward);
    msg::RegisterResp regresp;
    if (Usermodel::getinstance().zhuce(user))
    {
        regresp.set_ok(true);
        regresp.set_uid(user.clientid());
    }
    else
    {
        regresp.set_ok(false);
    }
    // 4. 序列化响应数据
    std::string resp_data;
    regresp.SerializeToString(&resp_data);

    // 5. 查找会话并发送响应
    // 通过 SessionManager 查找对应的会话
    std::shared_ptr<Session> session = SessionManager::getinstance().getSession(sessionid);
    if (session)
    {
        // 使用 SessionManager 的 buildMsg 函数将响应数据打包成网络数据包
        std::vector<char> response_package =
            SessionManager::getinstance().buildMsg(MSG_ZHUCEACK, resp_data);

        // 调用 Session 的 send 方法发送数据
        session->send(response_package);
    }
    // else: 如果会话找不到，说明客户端在处理期间断开了连接，无需发送。
}

// 登录消息处理函数
void MessageDispatcher::Denglu_handle(int sessionid, const std::string &data)
{
    msg::LoginReq loginreq;
    loginreq.ParseFromString(data);
    // 取出id与密码
    int uid = loginreq.uid();
    std::string password = loginreq.passwd();
    // 打印查看
    std::cout << "[Login] 接收到客户端登录请求 - UID: " << uid
              << " 密码: " << password << std::endl;
    GameUser user;
    user.setid(uid);
    user.setpaswd(password);
    msg::LoginResp loginresp;
    // 查询数据库是否存在账号和密码是否正确
    if (Usermodel::getinstance().Login(user))
    {
        // 账号密码正确
        //std::cout << "返回" << std::endl;
        SessionManager::getinstance().AddUser(uid, SessionManager::getinstance().getSession(sessionid));
        loginresp.set_ok(true);
    }
    else
    {
        loginresp.set_ok(false);
        loginresp.set_reason("账号或密码错误");
    }
    // 发送登录响应信息
    std::string loginres_data;
    loginresp.SerializePartialToString(&loginres_data);
    std::shared_ptr<Session> session = SessionManager::getinstance().getSession(sessionid);
    if (session)
    {
        // 使用 SessionManager 的 buildMsg 函数将响应数据打包成网络数据包
        std::vector<char> response_package =
            SessionManager::getinstance().buildMsg(MSG_DENGLUACK, loginres_data);

        // 调用 Session 的 send 方法发送数据
        session->send(response_package);
    }
}

// 用户数据查看消息处理函数
void MessageDispatcher::Backpack_handle(int sessionid, const std::string &data)
{
    msg::ViewPlayerDataReq datareq;
    datareq.ParseFromString(data);
    int uid = datareq.uid();
    // 获取uid玩家数据
    std::cout << "uid:" << uid << std::endl;

    auto playerdata = PlayerDataManager::getInstance().getPlayer(uid);

    if (playerdata)
    {
        // 返回数据
        std::cout << "返回玩家数据" << std::endl;
        std::shared_ptr<Session> session = SessionManager::getinstance().getSession(sessionid);
        if (session)
        {
            // 发送信息查看响应信息
            std::string User_data;
            playerdata->SerializePartialToString(&User_data);
            // 使用 SessionManager 的 buildMsg 函数将响应数据打包成网络数据包
            std::vector<char> response_package =
                SessionManager::getinstance().buildMsg(MSG_BACKPACKACK, User_data);
            std::cout << "send()" << std::endl;
            // 调用 Session 的 send 方法发送数据
            session->send(response_package);
        }
    }
    else
    {
        // 不存在玩家数据
        std::cout << "不存在玩家数据" << std::endl;
        //  如果玩家数据不存在，返回错误信息
        std::shared_ptr<Session> session = SessionManager::getinstance().getSession(sessionid);
        if (session)
        {
            // 构造错误信息
            std::string error_message = "Player data not found";

            // 使用 SessionManager 的 buildMsg 函数将响应数据打包成网络数据包
            std::vector<char> response_package =
                SessionManager::getinstance().buildMsg(MSG_BACKPACKACK, error_message);

            // 调用 Session 的 send 方法发送数据
            session->send(response_package);
        }
        return; // 如果玩家数据未找到，直接返回
    }
}

// 用户等级变化消息处理函数
void MessageDispatcher::AddExp_handle(int sessionid, const std::string &data)
{
    msg::AddExpReq req;
    if (!req.ParseFromString(data))
    {
        std::cerr << "[AddExp_handle] Parse data failed" << std::endl;
        return;
    }

    int uid = req.uid();
    int add_exp = req.exp_add();

    PlayerDataManager &datamanager = PlayerDataManager::getInstance();

    // 更新经验和等级，返回最新属性
    int new_level = 0;
    int new_exp = 0;
    bool leveled_up = false;

    datamanager.updateExepAndLevel(uid, add_exp, new_level, new_exp, leveled_up);

    std::cout << "构建返回经验信息" << std::endl;
    // 构造返回消息
    msg::AddExpRsp rsp;
    rsp.set_uid(uid);
    rsp.set_new_level(new_level);
    rsp.set_new_exp(new_exp);
    rsp.set_level_up(leveled_up);
    rsp.set_success(true);

    // 发送给客户端
    auto session = SessionManager::getinstance().getSession(sessionid);
    if (session)
    {
        std::string out;
        rsp.SerializeToString(&out);

        auto pkg = SessionManager::getinstance().buildMsg(MSG_ADDEXPACK, out);
        session->send(pkg);
    }
}

// 玩家加入房间
void MessageDispatcher::EnterRoom_handle(int sessionid, const std::string &data)
{
    msg::EnterRoomReq req;
    if (!req.ParseFromString(data))
    {
        std::cerr << "EnterRoomReq parse error" << std::endl;
        return;
    }

    int roomId = req.roomid();
    int uid = req.uid();

    auto session = SessionManager::getinstance().getSession(sessionid);
    if (!session)
    {
        std::cerr << "EnterRoom_handle: session not found!" << std::endl;
        return;
    }
    auto player = std::make_shared<Player>(uid, sessionid);

    auto &rm = RoomManager::getInstance();
    auto room = rm.getOrCreateRoom(roomId);

    bool ok = room->addPlayer(player);

    // 返回结果
    msg::EnterRoomAck ack;
    ack.set_ok(ok);
    ack.set_reason(ok ? "进入房间成功" : "进入房间失败，人满或已在房间");
    ack.set_roomid(roomId);

    std::string out;
    ack.SerializeToString(&out);

    auto pkg = SessionManager::getinstance().buildMsg(MSG_ENTER_ROOM_ACK, out);
    session->send(pkg);
}
// 玩家准备函数
void MessageDispatcher::Ready_handle(int sessionid, const std::string &data)
{
    msg::ReadyReq req;
    if (!req.ParseFromString(data))
    {
        std::cerr << "ReadyReq parse error" << std::endl;
        return;
    }

    int roomId = req.roomid();
    int uid = req.uid();
    bool ready = req.ready();

    auto &rm = RoomManager::getInstance();
    auto room = rm.getRoom(roomId);
    if (!room)
    {
        std::cerr << "Room not found!" << std::endl;
        return;
    }

    auto session = SessionManager::getinstance().getSession(sessionid);
    if (!session)
    {
        std::cerr << "Session not found!" << std::endl;
        return;
    }

    // 设置玩家准备状态
    room->setReady(uid, ready);

    // 返回准备状态
    msg::ReadyAck ack;
    ack.set_roomid(roomId);
    ack.set_uid(uid);
    ack.set_ready(ready);

    std::string out;
    ack.SerializeToString(&out);
    auto pkg = SessionManager::getinstance().buildMsg(MSG_READY_ACK, out);
    session->send(pkg);

    // 检查是否所有玩家都准备好了
    if (room->isAllReady()) // 如果所有玩家都准备好，开始战斗
    {
        // 战斗开始
        rm.startBattle(roomId);
    }
}

void MessageDispatcher::BattleAction_handle(int sessionid, const std::string &data)
{
    msg::BattleAction req;
    if (!req.ParseFromString(data))
    {
        std::cerr << "BattleAction parse error" << std::endl;
        return;
    }

    int roomId = req.roomid();
    int uid = req.uid();
    int skillId = req.skillid();
    int targetUid = req.target();

    auto &rm = RoomManager::getInstance();
    auto battle = rm.getBattleRoom(roomId);
    if (!battle)
    {
        std::cerr << "BattleRoom not found!" << std::endl;
        return;
    }

    // 示例：技能造成10点伤害
    int hpDelta = -10;
    int mpDelta = 0;

    battle->applyDelta(targetUid, hpDelta, mpDelta);

    // 构造 BattleSync
    msg::BattleSync sync;
    auto states = battle->getAllStates(); // 返回 uid -> hp/mp map
    for (auto &[uid, state] : states)
    {
        auto s = sync.add_states();
        s->set_uid(uid);
        s->set_hp(state.hp);
        s->set_mp(state.mp);
    }
    // sync.set_timestamp(current_ms());
    rm.broadcastRoom(req.roomid(), MSG_BATTLE_SYNC, sync.SerializeAsString());
}

