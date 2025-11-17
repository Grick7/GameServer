#include "UserDatamodel.h"
#include "protocol.pb.h"

#include <unordered_map>
UserDatamodel::UserDatamodel()
{
}
UserDatamodel &UserDatamodel::instance()
{
    static UserDatamodel userdatamodel;
    return userdatamodel;
}
bool UserDatamodel::QueryUserData(msg::PlayerAttr &playerdata)
{
    char sql[1024] = {0};
    snprintf(sql, sizeof sql, "select * from Player where uid=%d", playerdata.uid());
    MySQL mysql;
    if (mysql.connect())
    {
        // 连接成功 查询玩家数据
        if (MYSQL_RES *res = mysql.query(sql))
        {
            // 查询到结果
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                // 读取玩家数据
                playerdata.set_level(std::stoi(row[1]));
                playerdata.set_exp(std::stoi(row[2]));
                playerdata.set_hp(std::stoi(row[3]));
                playerdata.set_mp(std::stoi(row[4]));
                playerdata.set_coin(std::stoi(row[5]));
                playerdata.set_x(std::stof(row[6]));
                playerdata.set_y(std::stof(row[7]));
                playerdata.set_z(std::stof(row[8]));
                mysql_free_result(res);
                return true;
            }
            else
            {
                // 没查询到玩家数据
                mysql_free_result(res);
                std::cout << "玩家数据不存在" << std::endl;
                return false;
            }
        }
    }
    return false;
}

bool UserDatamodel::InsertUserData(msg::PlayerAttr &playerdata)
{
    MySQL mysql;
    if (mysql.connect())
    {
        char sql[1024] = {0};
        snprintf(sql, sizeof sql, "Insert into Player(uid,level,exp,hp,mp,coin,x,y,z)"
                                  "Values (%d,%d,%d,%d,%d,%d,%f,%f,%f);",
                 playerdata.uid(), playerdata.level(), playerdata.exp(), playerdata.hp(), playerdata.mp(),
                 playerdata.coin(), playerdata.x(), playerdata.y(), playerdata.z());
        if (mysql.update(sql))
        {
            // 插入数据成功
            return true;
        }
        else
        {
            return false;
        }
    }
    return false;
}

bool UserDatamodel::UpdateUserData(msg::PlayerAttr &playerdata)
{
    MySQL mysql;
    if (mysql.connect())
    {
        char sql[1024] = {0};
        // ⚠️ 确保 WHERE uid = ...，否则会更新整个表！
        snprintf(sql, sizeof(sql),
                 "UPDATE Player SET "
                 "level=%d, "
                 "exp=%d, "
                 "hp=%d, "
                 "mp=%d, "
                 "coin=%d, "
                 "x=%f, "
                 "y=%f, "
                 "z=%f "
                 "WHERE uid=%d;",
                 playerdata.level(),
                 playerdata.exp(),
                 playerdata.hp(),
                 playerdata.mp(),
                 playerdata.coin(),
                 playerdata.x(),
                 playerdata.y(),
                 playerdata.z(),
                 playerdata.uid());
        // 调用封装好的更新接口
        if (mysql.update(sql))
        {
            std::cout << "[MySQL] 玩家 " << playerdata.uid() << " 数据更新成功。" << std::endl;
            return true;
        }
        else
        {
            std::cerr << "[MySQL] 玩家 " << playerdata.uid() << " 数据更新失败: " << sql << std::endl;
            return false;
        }
    }
    std::cerr << "[MySQL] 连接失败，无法更新玩家 " << playerdata.uid() << " 数据。" << std::endl;
    return false;
}