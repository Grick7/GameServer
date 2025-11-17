#pragma once
#include "string"
#include <iostream>

#include "GameUser.h"
#include "DB.h"
class Usermodel
{
public:
    // 单例模式
    static Usermodel &getinstance();

    // 注册业务，向表中新添加一个user
    bool zhuce(GameUser &user);


    // 登录业务，向表中查询id与name是否对应
    bool Login(GameUser &user);


private:
    Usermodel()
    {

    }
};