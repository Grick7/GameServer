#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <mutex>

#include "public.h"
#include "GameUser.h"

struct Player
{
    int uid;
    int sessionid; // 对应 SessionManager 中的 session id
    int baseHp = 100;
    int baseMp = 50;

    Player(int u = 0, int sid = -1) : uid(u), sessionid(sid) {}
};
class Room
{
public:
    Room(int id, int maxPlayers = 2);

    //玩家加入房间
    bool addPlayer(std::shared_ptr<Player> player);
    //玩家退出
    void removePlayer(int uid);
    //玩家准备
    void setReady(int uid, bool ready);
    //房间是否满员
    bool isFull() const;
    //房间内玩家是否全部准备
    bool isAllReady() const;
    // 获取房间号
    int getId() const { return roomId_; }
    // 返回房间所有玩家
    std::vector<std::shared_ptr<Player>> getPlayersSnapshot() const;

private:
    // 房间号
    int roomId_;
    // 房间最大人数
    int maxPlayers_;
    mutable std::mutex mtx_; // 保护成员
    std::vector<std::shared_ptr<Player>> players_;
    // 记录了每个玩家的准备状态
    std::unordered_map<int, bool> readyState_;
};