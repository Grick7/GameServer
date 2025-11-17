#pragma once
#include "Session.h" 
#include <unordered_map>

#include <map>
#include <memory>
#include <vector>
#include <mutex>
class Session;

class SessionManager
{
public:
    static SessionManager &getinstance();

    void add(int id, std::shared_ptr<Session> s);

    void del(int id);

    // 新增：根据ID获取Session的共享指针
    std::shared_ptr<Session> getSession(int id);

    void guangbo(uint16_t msgid, const std::string &data);

    std::vector<char> buildMsg(uint16_t msgid, const std::string &data);

    //添加在线用户
    void AddUser(int uid,std::shared_ptr<Session> s);
    //删除离线用户
    void RemoveUser(int uid);
    //获取对应用户session
    std::shared_ptr<Session> getOnlineUser(int uid);


private:
    SessionManager();
    // 禁止复制
    SessionManager(const SessionManager &) = delete;
    SessionManager &operator=(const SessionManager &) = delete;

private:
    std::map<int, std::shared_ptr<Session>> sessions_;
    std::mutex mtx_;

    //存储在线用户
    std::unordered_map<int, std::shared_ptr<Session>> online_users_;
    std::mutex Users_mtx_;
};
