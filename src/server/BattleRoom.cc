#include "BattleRoom.h"
#include "SessionManager.h" // 假设项目里有这个用于发送数据
#include "protocol.pb.h"    // protobuf 生成的头（battle.proto 编译后）
#include <iostream>
#include "PlayerDataManager.h"

BattleRoom::BattleRoom(boost::asio::io_context &io, int roomid, const std::vector<std::shared_ptr<Player>> &players)
    : roomId_(roomid),
      io_(io),
      timer_(io),
      running_(false),
      players_(players)
{
    // 初始化战斗状态：读取玩家基础 hp/mp
    for (auto &p : players_)
    {
        states_[p->uid] = {p->baseHp, p->baseMp};
    }
}

BattleRoom::~BattleRoom()
{
    stop();
}
void BattleRoom::start()
{
    running_ = true;

    // ------------------------------
    // 1️⃣ 收集玩家 UID
    // ------------------------------
    std::vector<int> uids;
    for (auto &p : players_)
        uids.push_back(p->uid);

    // ------------------------------
    // 2️⃣ 从 Redis 批量获取玩家属性
    // ------------------------------
    std::unordered_map<int, msg::PlayerAttr> cachedData;
    PlayerDataManager::getInstance().batchLoadFromRedis(uids, cachedData);
    // cachedData: uid -> PlayerAttr { hp, mp, ... }

    // ------------------------------
    // 3️⃣ 对 Redis 中未命中的玩家，从 MySQL 批量加载
    // ------------------------------
    std::vector<int> missUids;
    for (int uid : uids)
        if (cachedData.find(uid) == cachedData.end())
            missUids.push_back(uid);

    if (!missUids.empty())
    {
        std::unordered_map<int, msg::PlayerAttr> dbData;
        PlayerDataManager::getInstance().batchLoadFromMySQL(missUids, dbData);
        // 合并数据
        cachedData.insert(dbData.begin(), dbData.end());
    }

    // ------------------------------
    // 4️⃣ 初始化 BattleRoom 内玩家状态
    // ------------------------------
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto &p : players_)
        {
            auto it = cachedData.find(p->uid);
            if (it != cachedData.end())
            {
                states_[p->uid] = State{it->second.hp(), it->second.mp()};
                p->baseHp = it->second.hp();
                p->baseMp = it->second.mp();
            }
            else
            {
                // 如果都没命中，使用默认值
                states_[p->uid] = State{100, 50};
                p->baseHp = 100;
                p->baseMp = 50;
            }
        }
    }

    // ------------------------------
    // 5️⃣ 广播 BattleStart
    // ------------------------------
    msg::BattleStart bs;
    bs.set_roomid(roomId_);
    for (auto &p : players_)
        bs.add_players(p->uid);
    std::string payload;
    bs.SerializePartialToString(&payload);

    auto pkg = SessionManager::getinstance().buildMsg(MSG_BATTLE_ACTION, payload);
    for (auto &p : players_)
    {
        auto s = SessionManager::getinstance().getSession(p->sessionid);
        if (s)
            s->send(pkg);
    }

    // ------------------------------
    // 6️⃣ 启动心跳同步
    // ------------------------------
    doHeartbeat();

    std::cout << "[BattleRoom] start room=" << roomId_
              << " with " << players_.size() << " players" << std::endl;
}

void BattleRoom::stop()
{
    running_ = false;
    boost::system::error_code ec;
    timer_.cancel(ec);
    std::cout << "[BattleRoom] stop room=" << roomId_ << std::endl;

    // 发送 BattleEnd（可定制 winner）
    msg::BattleEnd be;
    be.set_roomid(roomId_);
    be.set_winner(-1);
    std::string payload;
    be.SerializePartialToString(&payload);
    auto pkg = SessionManager::getinstance().buildMsg(MSG_BATTLE_SYNC, payload);
    for (auto &p : players_)
    {
        auto s = SessionManager::getinstance().getSession(p->sessionid);
        if (s)
            s->send(pkg);
    }
}
// 外部调用：按技能或动作修改目标的 hp/mp (可以为正或负)
void BattleRoom::applyDelta(int uid, int hpDelta, int mpDelta)
{
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = states_.find(uid);
    if (it == states_.end())
        return;
    it->second.hp = std::max(0, it->second.hp + hpDelta);
    it->second.mp = std::max(0, it->second.mp + mpDelta);
}

int BattleRoom::getHp(int uid)
{
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = states_.find(uid);
    if (it == states_.end())
        return 0;
    return it->second.hp;
}

int BattleRoom::getMp(int uid)
{
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = states_.find(uid);
    if (it == states_.end())
        return 0;
    return it->second.mp;
}
// 心跳函数，用于同步玩家血量、蓝量
void BattleRoom::doHeartbeat()
{
    if (!running_)
        return;

    auto self = shared_from_this();
    timer_.expires_after(heartbeatInterval_);
    timer_.async_wait([this, self](const boost::system::error_code &ec)
                      {
        if (ec) {
            if (ec == boost::asio::error::operation_aborted) return;
            std::cerr << "[BattleRoom] timer error: " << ec.message() << std::endl;
            return;
        }

        // 在心跳里广播当前状态
        broadcastSync();

        // 检查战斗结束
        checkFinish();

        // 继续下一次心跳
        if (running_) doHeartbeat(); });
}
// 广播玩家状态
void BattleRoom::broadcastSync()
{
    // 构造 protobuf 包
    msg::BattleSync sync;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto &kv : states_)
        {
            auto s = sync.add_states();
            s->set_uid(kv.first);
            s->set_hp(kv.second.hp);
            s->set_mp(kv.second.mp);
        }
    }
    sync.set_timestamp((int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count());

    std::string payload;
    sync.SerializePartialToString(&payload);

    auto pkg = SessionManager::getinstance().buildMsg(MSG_BATTLE_SYNC, payload);
    // 发送给房间内所有玩家
    for (auto &p : players_)
    {
        auto s = SessionManager::getinstance().getSession(p->sessionid);
        if (s)
            s->send(pkg);
    }
}
// 检查游戏是否结束
void BattleRoom::checkFinish()
{
    std::lock_guard<std::mutex> lk(mtx_);
    int alive = 0;
    int lastAliveUid = -1;
    for (auto &kv : states_)
    {
        if (kv.second.hp > 0)
        {
            alive++;
            lastAliveUid = kv.first;
        }
    }
    if (alive <= 1)
    {
        running_ = false;
        // 停掉 timer
        boost::system::error_code ec;
        timer_.cancel(ec);

        // 广播 BattleEnd（指出 winner）
        msg::BattleEnd be;
        be.set_roomid(roomId_);
        be.set_winner(alive == 1 ? lastAliveUid : -1);
        std::string payload;
        be.SerializePartialToString(&payload);
        auto pkg = SessionManager::getinstance().buildMsg(MSG_BATTLE_SYNC, payload);
        for (auto &p : players_)
        {
            auto s = SessionManager::getinstance().getSession(p->sessionid);
            if (s)
                s->send(pkg);
        }

        // 你可以在这里调用 PlayerDataManager 做经验、奖励分发、缓存/DB 同步等
        std::cout << "[BattleRoom] room " << roomId_ << " finished. winner=" << be.winner() << std::endl;
    }
}
