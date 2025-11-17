# GameServer
本项目是一个基于 C++ 语言在 Linux 系统下构建的高性能、模块化游戏服务器。我利用 Boost.Asio 库实现了异步 TCP 网络通信，并结合线程池和 Protobuf 协议，确保了网络层的高效与并发处理能力。在数据架构上，项目采用了 MySQL、Redis 和 Kafka 的专业组合：利用 Redis 提升玩家数据的读取效率，使用 MySQL 保证持久化，并通过 Kafka 实现数据的异步同步和最终一致性。整个项目结构清晰，按功能划分为登录注册、数据操作、实时通信等模块，并使用 CMakeLists 进行工程化管理，功能上实现了注册、登录、群聊、私聊以及玩家数据变化等核心业务逻辑。未来还会实现玩家对战等业务逻辑..
# 编译流程
cd build
cmake ..
make
# 可执行服务器
cd src/server
./MyServerExec
# 可执行客户端
cd build/src/client
./MyClientExec 127.0.0.1 8989

