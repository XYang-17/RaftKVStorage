cmake_minimum_required(VERSION 3.10)
project(CortExamples)

# 生成debug版本，可以进行gdb调试
set(CMAKE_BUILD_TYPE "Debug")

# 可执行文件输出目录 /bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# ----- 查找已安装的 raft 库和头文件 -----
set(LOCAL_DIR ${CMAKE_SOURCE_DIR}/../../local)

find_path(RAFT_INCLUDE_DIR raft PATHS ${LOCAL_DIR}/include NO_DEFAULT_PATH)
if(NOT RAFT_INCLUDE_DIR)
    message(FATAL_ERROR "未找到 raft 头文件，请先编译安装 src/raft（路径：${LOCAL_DIR}/include）")
endif()

# 添加头文件和库文件搜索路径
include_directories(${LOCAL_DIR}/include)
link_directories(${LOCAL_DIR}/lib)

#
add_executable(server src/server.cpp)
target_link_directories(server PRIVATE ${LOCAL_DIR})
target_link_libraries(server PRIVATE raft_core raft_rpcProto rpc cort boost_serialization muduo_base muduo_net protobuf pthread)
target_compile_options(server PRIVATE -g -O0 -fsanitize=address -fno-omit-frame-pointer)
target_link_options(server PRIVATE -rdynamic)
target_link_options(server PRIVATE -fsanitize=address)

add_executable(caller src/caller.cpp)
target_link_directories(caller PRIVATE ${LOCAL_DIR})
target_link_libraries(caller PRIVATE raft_clerk raft_rpcProto rpc cort boost_serialization muduo_base muduo_net protobuf pthread)