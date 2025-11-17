#include "Usermodel.h"


// 单例模式
Usermodel &Usermodel::getinstance()
{
    static Usermodel usermodel;
    return usermodel;
}

// 注册业务，向表中新添加一个user
bool Usermodel::zhuce(GameUser &user)
{
    // 拼sql语句
    char sql[1024] = {0};
    snprintf(sql, sizeof sql, "INSERT INTO User(name,passwd) VALUES('%s','%s')", user.name().c_str(), user.paswd().c_str());
    // 连接数据库
    MySQL mysql;
    if (mysql.connect())
    {
        // 连接成功
        if (mysql.update(sql))
        {
            // 获取注册用户的id
            user.setid(mysql_insert_id(mysql.getConnection()));
            return true;
        }
        else
        {
            std::cout << "注册失败" << std::endl;
            return false;
        }
    }
    std::cout << "mysql连接失败" << std::endl;
    return false;
}

// 登录业务，向表中查询id与pwd是否对应
bool Usermodel::Login(GameUser &user)
{
    char sql[1024] = {0};
    snprintf(sql, sizeof sql, "SELECT * FROM User WHERE uid=%d", user.clientid());
    MySQL mysql;
    if (mysql.connect())
    {
        // 数据库连接成功
        if (MYSQL_RES *res = mysql.query(sql))
        {
            // 查找成功
            //std::cout << "[DEBUG] 执行 SQL: " << sql << std::endl;
            //std::cout << "数据库查询成功" << std::endl;
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row)
            {
                //std::cout << "数据库查询到结果" << std::endl;
                std::string passwd = row[2];
                std::string name = row[1];
                user.setname(name);
                if (passwd == user.paswd())
                {
                    // 密码正确 登录成功；
                    mysql_free_result(res);
                    return true;
                }
            }
            std::cout << "数据库没有查询到结果" << std::endl;
            mysql_free_result(res);
        }
    }
    return false;
}