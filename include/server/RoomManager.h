#pragma once
#include <unordered_map>
#include <memory>
#include <mutex>
#include <boost/asio.hpp>
#include "Room.h"
#include "BattleRoom.h"

class RoomManager {
public:
    RoomManager(boost::asio::io_context& io);
    static RoomManager& getInstance(boost::asio::io_context* io = nullptr);

    std::shared_ptr<Room> getOrCreateRoom(int roomid);
    std::shared_ptr<Room> getRoom(int roomid);

    // 广播方便函数（按你的项目风格使用 buildMsg/send）
    void broadcastRoom(int roomid, int msgid, const std::string& data);

    // 当 room 满且准备好时由 dispatcher 调用
    void startBattle(int roomid);

    // 获取战斗房间（供 MessageDispatcher 使用）
    std::shared_ptr<BattleRoom> getBattleRoom(int roomid);

private:
    boost::asio::io_context& io_;
    std::mutex mtx_;
    std::unordered_map<int, std::shared_ptr<Room>> rooms_;
    std::unordered_map<int, std::shared_ptr<BattleRoom>> battles_;
};
