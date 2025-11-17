# src/server/CMakeLists.txt

# 明确列出服务器源文件
set(SERVER_SOURCES
    main.cc
    Session.cc
    DB.cc
    GameServer.cc
    SessionManager.cc
    MessageDispatcher.cc
    Usermodel.cc
    PlayerDataManager.cc
    Room.cc
    BattleRoom.cc
    RoomManager.cc
    UserDatamodel.cc
    ThreadPool.cc

)

# 1. 创建可执行目标，只包含手动编写的源文件
add_executable(MyServerExec ${SERVER_SOURCES})
# 设置 pkgconfig 路径
#set(CMAKE_PREFIX_PATH "/usr/local/lib/pkgconfig" ${CMAKE_PREFIX_PATH})
#set(CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu/pkgconfig" ${CMAKE_PREFIX_PATH})
# 2. 🌟 关键：链接 Protobuf 消息库 (替代了 target_sources)
# 移除 target_sources(<TargetName> PRIVATE ${PROTO_SRCS})
# 1. 查找 PKG-CONFIG 程序本身
find_package(PkgConfig REQUIRED)
# 2. 使用 pkg-config 查找 hiredis 库
#    这会设置 ${HIREDIS_LIBRARIES} 和 ${HIREDIS_INCLUDE_DIRS}
pkg_check_modules(HIREDIS REQUIRED hiredis)
# 添加 pkg-config 查找逻辑:
# 3. 🌟 关键修正 2: 确保头文件路径被包含 (hiredis 需要)
target_include_directories(MyServerExec
    PRIVATE
        ${HIREDIS_INCLUDE_DIRS}
        # ... 其他头文件路径
)
# 🌟 关键添加：手动将 vcpkg 的安装目录添加到 CMAKE_PREFIX_PATH
list(APPEND CMAKE_PREFIX_PATH 
    "/home/grick/package/vcpkg/vcpkg/installed/x64-linux"
)
# 1. 查找 cppkafka 包
# 这一行告诉 CMake 寻找 vcpkg 安装的 cppkafka 库
find_package(CppKafka REQUIRED)

# 2. 设置 CMake 查找库的路径

# ... (target_include_directories 保持不变) ...
# 替换为实际找到的完整路径
#set(NEW_HIREDIS_PATH "/usr/local/lib/libhiredis.so.1.3.0")

# ============== 链接依赖 ==============
target_link_libraries(MyServerExec 
    PUBLIC 
        CommonHeaders
    PRIVATE 
    # 强制链接新版本，并放在最前面
        #${NEW_HIREDIS_PATH}
        #Boost::system        
        #Boost::thread 
        ProtoMessages        
        ${MYSQL_LIBRARY}
        
        # Redis++ (已修正为正确的库名)
        redis++
        
        # 链接 hiredis 库 (使用 pkg-config 找到的变量)
        ${HIREDIS_LIBRARIES} 
        # 链接 CppKafka 目标（使用导出的目标名）
        CppKafka::cppkafka
)