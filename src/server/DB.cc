#include "DB.h"
#include <iostream>
// 初始化数据库连接
MySQL::MySQL()
{
    _conn = mysql_init(nullptr);
}

// 释放数据库连接资源
MySQL::~MySQL()
{
    if (_conn != nullptr)
    {
        mysql_close(_conn);
    }
}

// 连接数据库
bool MySQL::connect()
{
    MYSQL *p = mysql_real_connect(_conn, server.c_str(), user.c_str(), password.c_str(), dbname.c_str(), 3306, nullptr, 0);
    if (p != nullptr)
    {
        mysql_query(_conn, "set names gbk");
        std::cout << "connect mysql success!" << std::endl;
    }
    else
    {
        std::cout << "connect mysql fail!" << std::endl;
    }
    return p;
}
// 更新操作
bool MySQL::update(std::string sql)
{
    if (mysql_query(_conn, sql.c_str()))
    {
        std::cout << __FILE__ << ":" << __LINE__ << ": "
                  << sql << " 更新失败! 错误信息：" << mysql_error(_conn) << std::endl;
        return false;
    }
    return true;
}

// 查询操作
MYSQL_RES *MySQL::query(std::string sql)
{
    if (mysql_query(_conn, sql.c_str()))
    {
        std::cout << __FILE__ << ":" << __LINE__ << ":"
                  << sql << "查询失败！" << std::endl;
        return nullptr;
    }
    return mysql_use_result(_conn);
}

MYSQL *MySQL::getConnection()
{
    return _conn;
}