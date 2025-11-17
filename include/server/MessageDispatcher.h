#pragma once
#include <functional>
#include <unordered_map>
#include <string>
#include <iostream>

#include "SessionManager.h"
#include "protocol.pb.h"
#include "DB.h"
#include "GameUser.h"
#include "Usermodel.h"
#include "public.h"
#include "ThreadPool.h"
class MessageDispatcher
{
public:
    using MsgHandler = std::function<void(int sessionid, const std::string &)>;

    // ✅ 单例访问接口
    static MessageDispatcher &instance(ThreadPool &pool);

    // 注册(外部可以手动注册)
    void Register(int msg_id, MsgHandler handler);

    // 分发消息,调用先关函数
    void Dispatch(int sessionid, int msg_id, const std::string &data);

    // 新增：获取线程池引用
    ThreadPool &get_pool()
    {
        return pool_;
    }

    // 新增：用于获取已初始化的单例 (在 Session::close 等处调用)
    static MessageDispatcher &get_instance_initialized();

private:
    // ✅ 私有构造函数，自动注册 handler
    MessageDispatcher(ThreadPool &pool);

    // 禁止复制
    MessageDispatcher(const MessageDispatcher &) = delete;
    MessageDispatcher &operator=(const MessageDispatcher &) = delete;

    // 聊天消息处理函数
    void Chat_handle(int sessionid, const std::string &data);
    // 注册消息处理函数
    void Zhuce_handle(int sessionid, const std::string &data);
    // 登录消息处理函数
    void Denglu_handle(int sessionid, const std::string &data);
    // 用户数据查看消息处理函数
    void Backpack_handle(int sessionid, const std::string &data);
    // 通过增加的经验判断玩家等级消息处理函数
    void AddExp_handle(int sessionid, const std::string &data);
    // 玩家加入房间
    void EnterRoom_handle(int sessionid, const std::string &data);
    // 玩家准备函数
    void Ready_handle(int sessionid, const std::string &data);
    // 玩家战斗行为
    void BattleAction_handle(int sessionid, const std::string &data);

private:
    std::unordered_map<int, MsgHandler> handlers_;
    ThreadPool &pool_; // 对线程池的引用，用于投递任务
};
