#include "Room.h"
#include <algorithm>
#include <iostream>

Room::Room(int id, int maxPlayers) : roomId_(id), maxPlayers_(maxPlayers) {}

//玩家加入房间
bool Room::addPlayer(std::shared_ptr<Player> player)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if ((int)players_.size() >= maxPlayers_)
        return false;
    // 避免重复加入
    for (auto &p : players_)
    {
        if (p->uid == player->uid)
            return false;
    }
    players_.push_back(player);
    readyState_[player->uid] = false;
    std::cout << "[Room] player " << player->uid << " join room " << roomId_ << std::endl;
    return true;
}
//玩家退出房间
void Room::removePlayer(int uid)
{
    std::lock_guard<std::mutex> lk(mtx_);
    players_.erase(std::remove_if(players_.begin(), players_.end(),
                                  [&](const std::shared_ptr<Player> &p)
                                  { return p->uid == uid; }),
                   players_.end());
    readyState_.erase(uid);
}
//玩家准备
void Room::setReady(int uid, bool ready)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (readyState_.count(uid))
        readyState_[uid] = ready;
}
// 房间是否满员
bool Room::isFull() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return (int)players_.size() >= maxPlayers_;
}
// 玩家是否全部准备
bool Room::isAllReady() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    if ((int)players_.size() < maxPlayers_)
        return false;
    for (const auto &kv : readyState_)
    {
        if (!kv.second)
            return false;
    }
    return true;
}
// 返回房间内所有玩家
std::vector<std::shared_ptr<Player>> Room::getPlayersSnapshot() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return players_;
}
