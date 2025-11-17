#include "RoomManager.h"
#include "SessionManager.h"
#include "protocol.pb.h"
#include <iostream>

RoomManager::RoomManager(boost::asio::io_context& io) : io_(io) {}

RoomManager& RoomManager::getInstance(boost::asio::io_context* io) {
    static RoomManager* instance = nullptr;
    static std::mutex init_m;
    if (!instance) {
        std::lock_guard<std::mutex> lk(init_m);
        if (!instance) {
            if (!io) throw std::runtime_error("RoomManager first init requires io_context");
            instance = new RoomManager(*io);
        }
    }
    return *instance;
}
// 获取存在房间或者创建房间
std::shared_ptr<Room> RoomManager::getOrCreateRoom(int roomid) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!rooms_.count(roomid)) {
        rooms_[roomid] = std::make_shared<Room>(roomid);
    }
    return rooms_[roomid];
}
// 返回相应房间
std::shared_ptr<Room> RoomManager::getRoom(int roomid) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = rooms_.find(roomid);
    if (it == rooms_.end()) return nullptr;
    return it->second;
}

void RoomManager::broadcastRoom(int roomid, int msgid, const std::string& data) {
    auto room = getRoom(roomid);
    if (!room) return;
    auto players = room->getPlayersSnapshot();
    for (auto &p : players) {
        auto s = SessionManager::getinstance().getSession(p->sessionid);
        if (!s) continue;
        auto pkg = SessionManager::getinstance().buildMsg(msgid, data);
        s->send(pkg);
    }
}

void RoomManager::startBattle(int roomid) {
    std::shared_ptr<Room> room;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = rooms_.find(roomid);
        if (it == rooms_.end()) return;
        room = it->second;
        // 创建 BattleRoom 并保存
        if (battles_.count(roomid)) return; // 已有战斗
        auto players = room->getPlayersSnapshot();
        auto battle = std::make_shared<BattleRoom>(io_, roomid, players);
        battles_[roomid] = battle;
        battle->start();
    }
}

std::shared_ptr<BattleRoom> RoomManager::getBattleRoom(int roomid) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = battles_.find(roomid);
    if (it == battles_.end()) return nullptr;
    return it->second;
}
