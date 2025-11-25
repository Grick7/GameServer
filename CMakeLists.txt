# ProjectRoot/CMakeLists.txt

# 1. 🌟 必须是第一行：定义所需的最小 CMake 版本
cmake_minimum_required(VERSION 3.10)

# 2. 🌟 必须是第二行：定义项目名称
project(mygameserver CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ============== 外部依赖查找 (Boost 和 Protobuf) ==============
#find_package(Boost 1.70 REQUIRED COMPONENTS system thread)
# 3. 🌟 必须在调用 protobuf_generate_cpp() 之前执行！
find_package(Protobuf REQUIRED)
# 3. 🌟 必须在调用 protobuf_generate_cpp() 之前执行！
find_package(Protobuf REQUIRED) 

# ==================== Protobuf 编译自动化与库定义 ====================

set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
file(GLOB PROTO_FILES "${PROTO_SRC_DIR}/*.proto")

# 现在函数已被加载，可以安全调用
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# 创建一个静态库来编译所有生成的 Protobuf 源文件
add_library(ProtoMessages STATIC ${PROTO_SRCS})
target_link_libraries(ProtoMessages PUBLIC ${Protobuf_LIBRARIES})

# ==================== 公共头文件接口定义 ====================

# 定义 CommonHeaders 接口库
add_library(CommonHeaders INTERFACE)

target_include_directories(CommonHeaders INTERFACE 
    # 公共头文件路径：/include 目录下的文件
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/server>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/client>
    # Protobuf 生成头文件（.pb.h）的路径
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
set(CMAKE_PREFIX_PATH 
    "/usr/local" 
    "/usr" 
    "/usr/local/lib" 
    "/usr/lib" # 确保 /usr/lib 也被包含
    "/usr/lib/x86_64-linux-gnu" 
    ${CMAKE_PREFIX_PATH}
)

set(MYSQL_LIBRARY "/usr/lib/x86_64-linux-gnu/libmysqlclient.so")


# ============== 添加子目录 ==============
add_subdirectory(src/client)
add_subdirectory(src/server)