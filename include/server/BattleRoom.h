#pragma once
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>
#include <boost/asio.hpp>
#include "Room.h"

// 前向声明 SessionManager（你项目里已有）
class SessionManager;

class BattleRoom : public std::enable_shared_from_this<BattleRoom>
{
public:
    BattleRoom(boost::asio::io_context &io, int roomid, const std::vector<std::shared_ptr<Player>> &players);
    ~BattleRoom();
    struct State
    {
        int hp;
        int mp;
    };
    void start(); // 启动心跳与战斗循环
    void stop();  // 停止战斗

    // 外部调用：按技能或动作修改目标的 hp/mp (可以为正或负)
    void applyDelta(int uid, int hpDelta, int mpDelta);
    // 获取玩家血量
    int getHp(int uid);
    // 获取玩家蓝量
    int getMp(int uid);
    std::unordered_map<int, State> getAllStates() const
    {
        return states_;
    }

private:
    void doHeartbeat();   // 心跳函数，用于定期同步玩家血量、蓝量
    void broadcastSync(); // 构造并广播 BattleSync protobuf
    void checkFinish();   // 检查是否有玩家死亡以结束战斗

private:
    int roomId_;
    boost::asio::io_context &io_;
    boost::asio::steady_timer timer_;
    std::atomic<bool> running_;

    std::mutex mtx_; // 保护状态
    std::vector<std::shared_ptr<Player>> players_;

    // 记录玩家hp、mp
    std::unordered_map<int, State> states_; // uid -> state

    // 心跳间隔（ms）
    const std::chrono::milliseconds heartbeatInterval_{50};
};
