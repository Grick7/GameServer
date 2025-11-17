#pragma once
#include "DB.h"
#include"protocol.pb.h"
class UserDatamodel
{

public:
    static UserDatamodel& instance();
    bool QueryUserData(msg::PlayerAttr &playerdata);

    bool UpdateUserData(msg::PlayerAttr &playerdata);

    bool InsertUserData(msg::PlayerAttr &playerdata);
private:
    UserDatamodel();
};