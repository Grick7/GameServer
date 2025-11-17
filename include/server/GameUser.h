#pragma once
#include "string"

class GameUser
{
public:
    void setstate(bool statue)
    {
        state_ = statue;
    }
    void setid(int id)
    {
        clientid_ = id;
    }
    void setname(std::string name)
    {
        name_ = name;
    }
    std::string name()
    {
        return name_;
    }
    int clientid()
    {
        return clientid_;
    }
    bool state()
    {
        return state_;
    }
    std::string paswd()
    {
        return paswd_;
    }

    void setpaswd(std::string paswd)
    {
        paswd_ = paswd;
    }

private:
    std::string name_;
    int clientid_;
    bool state_;
    std::string paswd_;
};
