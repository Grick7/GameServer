#include "PlayerDataManager.h"
#include "UserDatamodel.h"

#include <unordered_map>
PlayerDataManager::PlayerDataManager()
{
    // 配置Redis单个连接的信息
    sw::redis::ConnectionOptions redis_opts;
    redis_opts.host = "127.0.0.1";
    redis_opts.port = 6379;
    redis_opts.socket_timeout = std::chrono::milliseconds(200); // 设置操作的超时时间，如果200ms内没有响应，返回超时错误。

    // 配置Redis连接池信息
    sw::redis::ConnectionPoolOptions pool_opts;
    pool_opts.size = 8;                               // 最大连接数
    pool_opts.wait_timeout = std::chrono::seconds(2); // 设置线程等待空闲链接的最大时间，如果超过2s没获取连接，返回

    // 创建带连接池的Redis客户端
    redis_ = std::make_shared<sw::redis::Redis>(redis_opts, pool_opts);

    // 配置KafKa
    cppkafka::Configuration kafka_config = {
        {"metadata.broker.list", "127.0.0.1:9092"}};
    producer_ = std::make_shared<cppkafka::Producer>(kafka_config);
    topic_ = "playerdata_update"; // 玩家数据更新的消息都会发送到该topic
}

PlayerDataManager &PlayerDataManager::getInstance()
{
    static PlayerDataManager instance;
    return instance;
}

// 玩家登录获取数据
std::shared_ptr<msg::PlayerAttr> PlayerDataManager::loadPlayerData(int uid)
{
    auto player = getPlayer(uid);
    if (!player)
    {
        // 新玩家，初始化数据
        std::cout << "[NewPlayer] 玩家 " << uid << " 首次登录，创建默认数据\n";
        auto playerdata = std::make_shared<msg::PlayerAttr>();
        playerdata->set_uid(uid);
        playerdata->set_level(1);
        playerdata->set_exp(0);
        playerdata->set_hp(100);
        playerdata->set_mp(50);
        playerdata->set_coin(1000);
        playerdata->set_x(0.0f);
        playerdata->set_y(0.0f);
        playerdata->set_z(0.0f);

        // 插入新玩家数据到 MySQL
        if (UserDatamodel::instance().InsertUserData(*playerdata))
        {

            // 将多个字段和值存入 std::map 中
            std::unordered_map<std::string, std::string> player_data = {
                {"level", std::to_string(playerdata->level())},
                {"exp", std::to_string(playerdata->exp())},
                {"hp", std::to_string(playerdata->hp())},
                {"mp", std::to_string(playerdata->mp())},
                {"coin", std::to_string(playerdata->coin())},
                {"x", std::to_string(playerdata->x())},
                {"y", std::to_string(playerdata->y())},
                {"z", std::to_string(playerdata->z())}};
            std::string redis_key = "player:" + std::to_string(uid);
            // 使用 hset 方法将多个字段及其值保存到 Redis
            redis_->hset(redis_key, player_data.begin(), player_data.end());
        }
        return playerdata;
    }

    return player;
}

// 获取玩家数据
std::shared_ptr<msg::PlayerAttr> PlayerDataManager::getPlayer(int uid)
{
    // 获取玩家级互斥锁（保证单个玩家线程安全）
    std::shared_ptr<std::mutex> player_mtx;
    // 初始化玩家级 mutex
    {
        std::lock_guard<std::mutex> lock(mtx_map_);
        if (player_mutex_map_.find(uid) == player_mutex_map_.end())
        {
            player_mutex_map_.emplace(uid, std::make_shared<std::mutex>());
        }
        player_mtx = player_mutex_map_[uid];
    }
    // 先尝试从缓存中读取
    std::string redis_key = "player:" + std::to_string(uid);
    std::shared_ptr<msg::PlayerAttr> playerdata = std::make_shared<msg::PlayerAttr>();
    playerdata->set_uid(uid);
    std::lock_guard<std::mutex> lock(*player_mtx);
    // 判断是否在缓存中
    if (redis_->exists(redis_key))
    {
        std::cout << "从Redis中读取数据" << std::endl;
        // 在,把数据读出
        std::unordered_map<std::string, std::string> data;
        redis_->hgetall(redis_key, std::inserter(data, data.end())); // 获取Redis中key所对应的值
        // 遍历map获取数据
        playerdata->set_level(std::stoi(data["level"]));
        playerdata->set_exp(std::stoi(data["exp"]));
        playerdata->set_hp(std::stoi(data["hp"]));
        playerdata->set_mp(std::stoi(data["mp"]));
        playerdata->set_coin(std::stoi(data["coin"]));
        playerdata->set_x(std::stof(data["x"]));
        playerdata->set_y(std::stof(data["y"]));
        playerdata->set_z(std::stof(data["z"]));
        return playerdata;
    }

    // 不在缓存中，从数据库查询，并更新缓存；
    if (UserDatamodel::instance().QueryUserData(*playerdata))
    {
        // 查询成功，更新缓存
        // 将多个字段和值存入 std::map 中
        std::unordered_map<std::string, std::string> player_data = {
            {"level", std::to_string(playerdata->level())},
            {"exp", std::to_string(playerdata->exp())},
            {"hp", std::to_string(playerdata->hp())},
            {"mp", std::to_string(playerdata->mp())},
            {"coin", std::to_string(playerdata->coin())},
            {"x", std::to_string(playerdata->x())},
            {"y", std::to_string(playerdata->y())},
            {"z", std::to_string(playerdata->z())}};

        // 使用 hset 方法将多个字段及其值保存到 Redis
        redis_->hset(redis_key, player_data.begin(), player_data.end());
        return playerdata;
    }

    // ⬅️ **现在这个条件是正确的了！**
    std::cout << "不存在玩家数据" << std::endl;

    // 构造错误信息并发送
    // ... (发送错误信息的逻辑，请注意 sessionid 的来源) ...

    return nullptr; // ⬅️ 返回空指针表示失败
}

// 更新属性
void PlayerDataManager::updatePlayerAttr(int uid, const std::string &field, int value)
{
    // 获取玩家锁
    std::shared_ptr<std::mutex> player_mutex;
    {
        std::lock_guard<std::mutex> lock(mtx_map_);
        // 查找uid对应的玩家锁
        if (player_mutex_map_.find(uid) == player_mutex_map_.end())
        {
            // 没找到
            player_mutex_map_.emplace(uid, std::make_shared<std::mutex>());
        }
        player_mutex = player_mutex_map_[uid];
    }
    // 加锁进行操作 保证同一个玩家的操作串行执行
    std::lock_guard<std::mutex> lock(*player_mutex);
    std::string redis_key = "player:" + std::to_string(uid);
    // 检查redis中是否存在对应数据
    if (!redis_->exists(redis_key))
    {
        std::cout << "[RedisMiss] 玩家 " << uid << " 数据不存在，无法更新。" << std::endl;
        return;
    }
    // 更新redis中玩家数据
    try
    {
        redis_->hset(redis_key, field, std::to_string(value));
        std::cout << "[RedisUpdate] 玩家 " << uid
                  << " 字段 " << field
                  << " 更新为 " << value << std::endl;
    }
    catch (const sw::redis::Error &err)
    {
        std::cerr << "[RedisError] 更新玩家 " << uid
                  << " 失败：" << err.what() << std::endl;
        return;
    }
    // 将数据变化信息写入kafka，用于异步同步mysql
    try
    {
        /* code */
        std::string msg = std::to_string(uid) + "|" + field + "|" + std::to_string(value); // 写入格式 uid|属性名|值
        cppkafka::MessageBuilder builder(topic_);                                          // 用于构造消息的对象 builder
        builder.payload(msg);                                                              // kafka消费者队列接收的消息

        producer_->produce(builder); // 将消息放到生产者内部队列，异步将消息发送到kafka
        producer_->flush();          // 保证及时送出消息
        std::cout << "[Kafka] 推送玩家 " << uid << " 更新消息：" << msg << std::endl;
    }
    catch (const cppkafka::Exception &ex)
    {
        std::cerr << "[KafkaError] 推送玩家 " << uid << " 数据失败：" << ex.what() << std::endl;
    }
}

// 同步redis-》mysql
void PlayerDataManager::syncToMySQL(int uid, const std::string &field, int value)
{
    // 在kafka消费队列消费时调用 同步mysql与redis中的数据
    // 获取玩家锁
    std::shared_ptr<std::mutex> playermutex;
    {
        std::lock_guard<std::mutex> lock(mtx_map_);
        if (player_mutex_map_.find(uid) == player_mutex_map_.end())
        {
            // 没找到
            player_mutex_map_[uid] = std::make_shared<std::mutex>();
        }
        playermutex = player_mutex_map_[uid];
    }
    std::lock_guard<std::mutex> lock(*playermutex);

    std::string redis_key = "player:" + std::to_string(uid);
    try
    {
        /* code */
        if (!redis_->exists(redis_key))
        {
            std::cout << "[syncToMySQL] 玩家 " << uid << " Redis 数据不存在，跳过同步" << std::endl;
            return;
        }

        // 获取 Redis 数据
        std::unordered_map<std::string, std::string> data;
        redis_->hgetall(redis_key, std::inserter(data, data.end()));

        std::shared_ptr<msg::PlayerAttr> player = std::make_shared<msg::PlayerAttr>();
        // 遍历map获取数据
        player->set_uid(uid);
        player->set_level(std::stoi(data["level"]));
        player->set_exp(std::stoi(data["exp"]));
        player->set_hp(std::stoi(data["hp"]));
        player->set_mp(std::stoi(data["mp"]));
        player->set_coin(std::stoi(data["coin"]));
        player->set_x(std::stof(data["x"]));
        player->set_y(std::stof(data["y"]));
        player->set_z(std::stof(data["z"]));
        // 在mysql更新
        UserDatamodel::instance().UpdateUserData(*player);
    }
    catch (const sw::redis::Error &err)
    {
        std::cerr << "[RedisError] 玩家 " << uid << " 同步 MySQL 读取 Redis 失败：" << err.what() << std::endl;
    }
}

// 经验增加更新函数
void PlayerDataManager::updateExepAndLevel(int uid, int addexep, int &new_level, int &new_exp, bool &leveled_up)
{
    // 获取玩家锁
    std::shared_ptr<std::mutex> player_mutex;
    {
        std::lock_guard<std::mutex> lock(mtx_map_);
        // 查找uid对应的玩家锁
        if (player_mutex_map_.find(uid) == player_mutex_map_.end())
        {
            // 没找到
            player_mutex_map_.emplace(uid, std::make_shared<std::mutex>());
        }
        player_mutex = player_mutex_map_[uid];
    }
    // 加锁进行操作 保证同一个玩家的操作串行执行
    std::lock_guard<std::mutex> lock(*player_mutex);
    // 查找redis，
    std::string redis_key = "player:" + std::to_string(uid);

    if (!redis_->exists(redis_key))
    {
        std::cout << "[RedisMiss] 玩家 " << uid << " 数据不存在，从MySQL加载..." << std::endl;
        std::shared_ptr<msg::PlayerAttr> playerdata = std::make_shared<msg::PlayerAttr>();
        playerdata->set_uid(uid);
        // 不在缓存中，从数据库查询，并更新缓存；
        if (UserDatamodel::instance().QueryUserData(*playerdata))
        {
            // 查询成功，更新缓存
            // 将多个字段和值存入 std::map 中
            std::unordered_map<std::string, std::string> player_data = {
                {"level", std::to_string(playerdata->level())},
                {"exp", std::to_string(playerdata->exp())},
                {"hp", std::to_string(playerdata->hp())},
                {"mp", std::to_string(playerdata->mp())},
                {"coin", std::to_string(playerdata->coin())},
                {"x", std::to_string(playerdata->x())},
                {"y", std::to_string(playerdata->y())},
                {"z", std::to_string(playerdata->z())}};
            // 使用 hset 方法将多个字段及其值保存到 Redis
            redis_->hset(redis_key, player_data.begin(), player_data.end());
        }
    }
    // 读取当前经验与等级
    auto exp_str = redis_->hget(redis_key, "exp");
    auto level_str = redis_->hget(redis_key, "level");
    int exp = exp_str ? std::stoi(*exp_str) : 0;
    int level = level_str ? std::stoi(*level_str) : 1;

    exp += addexep;
    bool levelUp = false;
    int nextLevelExp = 100 * level;
    while (exp >= nextLevelExp)
    {
        exp -= nextLevelExp;
        level++;
        nextLevelExp = 100 * level;
        levelUp = true;
        leveled_up = true;
    }
    new_level = level;
    new_exp = exp;
    // Step 4: 更新Redis
    std::unordered_map<std::string, std::string> updates = {
        {"exp", std::to_string(exp)},
        {"level", std::to_string(level)}};
    redis_->hset(redis_key, updates.begin(), updates.end());

    std::cout << "[ExpUpdate] 玩家 " << uid
              << " 当前经验：" << exp
              << " 等级：" << level
              << (levelUp ? " (升级啦！)" : "") << std::endl;

    // Step 5: Kafka 异步同步 MySQL
    try
    {
        std::string msg = std::to_string(uid) + "|exp|" + std::to_string(exp);
        producer_->produce(cppkafka::MessageBuilder(topic_).payload(msg));

        if (levelUp)
        {
            std::string msg2 = std::to_string(uid) + "|level|" + std::to_string(level);
            producer_->produce(cppkafka::MessageBuilder(topic_).payload(msg2));
        }

        producer_->flush();
    }
    catch (const cppkafka::Exception &ex)
    {
        std::cerr << "[KafkaError] 玩家 " << uid << " 数据推送失败：" << ex.what() << std::endl;
    }
}

// 批量从redis中获取玩家数据
void PlayerDataManager::batchLoadFromRedis(
    const std::vector<int> &uids,
    std::unordered_map<int, msg::PlayerAttr> &out)
{
    for (int uid : uids)
    {
        std::shared_ptr<std::mutex> player_mtx;
        {
            std::lock_guard<std::mutex> lock(mtx_map_);
            if (player_mutex_map_.find(uid) == player_mutex_map_.end())
                player_mutex_map_.emplace(uid, std::make_shared<std::mutex>());
            player_mtx = player_mutex_map_[uid];
        }

        std::lock_guard<std::mutex> lock(*player_mtx);
        std::string redis_key = "player:" + std::to_string(uid);

        if (!redis_->exists(redis_key))
            continue; // Redis 中不存在，留给 MySQL 处理

        std::unordered_map<std::string, std::string> data;
        redis_->hgetall(redis_key, std::inserter(data, data.end()));

        msg::PlayerAttr playerdata;
        playerdata.set_uid(uid);
        playerdata.set_level(std::stoi(data["level"]));
        playerdata.set_exp(std::stoi(data["exp"]));
        playerdata.set_hp(std::stoi(data["hp"]));
        playerdata.set_mp(std::stoi(data["mp"]));
        playerdata.set_coin(std::stoi(data["coin"]));
        playerdata.set_x(std::stof(data["x"]));
        playerdata.set_y(std::stof(data["y"]));
        playerdata.set_z(std::stof(data["z"]));

        out[uid] = playerdata;
    }
}

// 批量从mysql中获取数据并且加入到redis中
void PlayerDataManager::batchLoadFromMySQL(
    const std::vector<int> &uids,
    std::unordered_map<int, msg::PlayerAttr> &out)
{
    for (int uid : uids)
    {
        std::shared_ptr<std::mutex> player_mtx;
        {
            std::lock_guard<std::mutex> lock(mtx_map_);
            if (player_mutex_map_.find(uid) == player_mutex_map_.end())
                player_mutex_map_.emplace(uid, std::make_shared<std::mutex>());
            player_mtx = player_mutex_map_[uid];
        }

        std::lock_guard<std::mutex> lock(*player_mtx);

        msg::PlayerAttr playerdata;
        playerdata.set_uid(uid);

        // 查询数据库
        if (!UserDatamodel::instance().QueryUserData(playerdata))
        {
            std::cout << "玩家 uid=" << uid << " 不存在数据库" << std::endl;
            continue;
        }

        // 写回 Redis
        std::string redis_key = "player:" + std::to_string(uid);
        std::unordered_map<std::string, std::string> redis_data = {
            {"level", std::to_string(playerdata.level())},
            {"exp", std::to_string(playerdata.exp())},
            {"hp", std::to_string(playerdata.hp())},
            {"mp", std::to_string(playerdata.mp())},
            {"coin", std::to_string(playerdata.coin())},
            {"x", std::to_string(playerdata.x())},
            {"y", std::to_string(playerdata.y())},
            {"z", std::to_string(playerdata.z())}};
        redis_->hset(redis_key, redis_data.begin(), redis_data.end());

        out[uid] = playerdata;
    }
}