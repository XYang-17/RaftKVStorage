cmake_minimum_required(VERSION 3.10)
project(RpcExamples)

# ----- 设置可执行文件输出目录（/bin） -----
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# ----- 查找已安装的 rpc 库和头文件 -----
set(LOCAL_DIR ${CMAKE_SOURCE_DIR}/../../local)

find_library(RPC_LIB rpc PATHS ${LOCAL_DIR}/lib NO_DEFAULT_PATH)
if(NOT RPC_LIB)
    message(FATAL_ERROR "未找到 rpc 库，请先编译安装 src/rpc（路径：${LOCAL_DIR}/lib）")
endif()

find_path(RPC_INCLUDE_DIR rpc PATHS ${LOCAL_DIR}/include NO_DEFAULT_PATH)
if(NOT RPC_INCLUDE_DIR)
    message(FATAL_ERROR "未找到 rpc 头文件，请先编译安装 src/rpc（路径：${LOCAL_DIR}/include）")
endif()

# 生成debug版本，可以进行gdb调试
set(CMAKE_BUILD_TYPE "Debug")

# ----- 包含头文件路径 -----
# 1. examples/rpc/include 中的测试用例公用头文件
# 2. local/include 中的 rpc 库头文件
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LOCAL_DIR}/include
)

# ----- 收集公共源文件（位于 examples/rpc 下，不含 src 子目录）-----
file(GLOB SHARED_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

# 将公共源文件编译成静态库，方便复用（避免每个可执行文件重复编译）
add_library(test_common STATIC ${SHARED_SOURCES})
target_include_directories(test_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LOCAL_DIR}/include
)

# ----- 处理 src/ 下的每个测试用例 -----
file(GLOB MAIN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

foreach(MAIN_FILE ${MAIN_SOURCES})
    # 获取文件名（不含扩展名）作为可执行文件名
    get_filename_component(EXE_NAME ${MAIN_FILE} NAME_WE)
    
    # 为每个源文件生成独立的可执行文件
    add_executable(${EXE_NAME} ${MAIN_FILE})
    
    # 链接公共库和 rpc 静态库
    target_link_libraries(${EXE_NAME}
        test_common
        ${RPC_LIB}
    )
    
    # 如果需要其他库
    target_link_libraries(${EXE_NAME}
        /usr/local/lib/libboost_serialization.so
        /usr/local/lib/libprotobuf.so
        muduo_base muduo_net )
endforeach()

# 可选：打印生成的可执行文件列表
message(STATUS "已为以下测试用例生成目标：")
foreach(MAIN_FILE ${MAIN_SOURCES})
    get_filename_component(EXE_NAME ${MAIN_FILE} NAME_WE)
    message(STATUS "  ${EXE_NAME}")
endforeach()