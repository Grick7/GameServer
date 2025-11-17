# src/client/CMakeLists.txt

# 明确列出客户端源文件
set(CLIENT_SOURCES
    client.cc
)

# 🌟 关键修正：在链接 Boost 之前，必须先查找它
find_package(Boost 1.70 REQUIRED COMPONENTS system thread)
# 1. 创建可执行目标，只包含手动编写的源文件
add_executable(MyClientExec ${CLIENT_SOURCES})

# 2. 🌟 关键：链接 Protobuf 消息库 (替代了 target_sources)
# 移除 target_sources(<TargetName> PRIVATE ${PROTO_SRCS})

# ... (target_include_directories 保持不变) ...

# ============== 链接依赖 ==============
target_link_libraries(MyClientExec 
    PUBLIC 
        CommonHeaders
    PRIVATE 
        Boost::system      
        Boost::thread 
        ProtoMessages        # 🌟 链接我们新创建的库
)