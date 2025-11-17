#pragma once

enum msgid
{
    MSG_CHAT = 1,
    MSG_CHATACK,
    MSG_ZHUCE,
    MSG_ZHUCEACK,  // 注册响应信息
    MSG_DENGLU,    // 登录信息
    MSG_DENGLUACK, // 登录响应信息
    MSG_BACKPACK,   // 查看玩家数据信息
    MSG_BACKPACKACK, //玩家信息相应消息
    MSG_ADDEXP,//玩家等级变化信息
    MSG_ADDEXPACK,
        // --- 新增对战相关 ---
    MSG_ENTER_ROOM,    // 玩家请求进入房间
    MSG_ENTER_ROOM_ACK,//进入房间响应
    MSG_READY,         // 玩家准备
    MSG_READY_ACK,//玩家准备响应
    MSG_BATTLE_ACTION, // 战斗开始
    MSG_BATTLE_SYNC//服务器广播血量、蓝量变化
};
enum class RoomStatus
{
    WAITING, // 等待玩家
    FIGHTING // 战斗中
};