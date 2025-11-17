#pragma once

#include "protocol.pb.h"
#include "GameUser.h"
#include "UserDatamodel.h"

#include <sw/redis++/redis++.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <cppkafka/cppkafka.h>
#include<unordered_map>

class PlayerDataManager
{
public:
    static PlayerDataManager &getInstance();

    // 玩家登录获取数据
    std::shared_ptr<msg::PlayerAttr> loadPlayerData(int uid);
    // 获取玩家数据
    std::shared_ptr<msg::PlayerAttr> getPlayer(int uid);
    // 更新属性
    void updatePlayerAttr(int uid, const std::string &field, int value);
    //批量从redis中获取玩家数据
    void batchLoadFromRedis(const std::vector<int> &uids, std::unordered_map<int, msg::PlayerAttr> &out);
    //批量从mysql中获取数据并且加入到redis中
    void batchLoadFromMySQL(const std::vector<int> &uids, std::unordered_map<int, msg::PlayerAttr> &out);
    // 经验增加更新函数
    void updateExepAndLevel(int uid, int addexep, int &new_level, int &new_exp, bool &leveled_up);
    // 同步redis-》mysql
    void syncToMySQL(int uid, const std::string &field, int value);
    // 定时全量同步
    void syncAll();
    // 玩家下线
    void playerLogout(int uid);

private:
    PlayerDataManager();

    std::shared_ptr<sw::redis::Redis> redis_;

    // kafka
    std::shared_ptr<cppkafka::Producer> producer_; // 生产者模型 用于写队列。
    std::string topic_;

    std::mutex mtx_map_; // 保护 player_mutex_map_ 的访问
    std::unordered_map<int, std::shared_ptr<std::mutex>> player_mutex_map_;
};