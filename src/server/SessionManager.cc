#include "SessionManager.h"
#include "Session.h"
SessionManager::SessionManager()
{
}
SessionManager &SessionManager::getinstance()
{
    static SessionManager sessionmanager;
    return sessionmanager;
}
void SessionManager::add(int id, std::shared_ptr<Session> s)
{
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_[id] = s;
}

void SessionManager::del(int id)
{
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_.erase(id);
}

// 新增：根据ID获取Session的共享指针
std::shared_ptr<Session> SessionManager::getSession(int id)
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(id);
    if (it != sessions_.end())
    {
        return it->second;
    }
    return nullptr; // 找不到则返回空
}
// 获取用户Session
std::shared_ptr<Session> SessionManager::getOnlineUser(int uid)
{
    std::lock_guard<std::mutex> lock(Users_mtx_);
    auto it = online_users_.find(uid);
    if (it != online_users_.end())
        return it->second;
    return nullptr;
}

void SessionManager::guangbo(uint16_t msgid, const std::string &data)
{
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto &p : online_users_)
    {
        if (p.first != -1)
        {
            p.second->send(buildMsg(msgid, data));
        }
    }
}
std::vector<char> SessionManager::buildMsg(uint16_t msgid, const std::string &data)
{
    // 整个 Body 的长度 (包括 2 字节 MsgID 和数据)
    uint32_t len = data.size() + sizeof(uint16_t);
    std::vector<char> p(sizeof(uint32_t) + len);

    // --- 关键修复 1: 转换长度为网络字节序 ---
    uint32_t network_order_len = htonl(len);
    // 复制长度 Header (4 字节)
    memcpy(p.data(), &network_order_len, sizeof(uint32_t));

    // --- 关键修复 2: 转换 MsgID 为网络字节序 ---
    uint16_t network_order_msgid = htons(msgid);
    // 复制 MsgID (2 字节)
    memcpy(p.data() + sizeof(uint32_t), &network_order_msgid, sizeof(uint16_t));

    // 复制 Protobuf 数据 (Body)
    memcpy(p.data() + sizeof(uint32_t) + sizeof(uint16_t), data.data(), data.size());

    return p;
}

// 添加在线用户
void SessionManager::AddUser(int uid, std::shared_ptr<Session> s)
{
    std::lock_guard<std::mutex> lock(Users_mtx_);
    online_users_[uid] = s;
}
// 删除离线用户
void SessionManager::RemoveUser(int uid)
{
    std::lock_guard<std::mutex> lock(Users_mtx_);
    online_users_.erase(uid);
}
