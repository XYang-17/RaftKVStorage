cmake_minimum_required(VERSION 3.10)
project(raft VERSION 1.0.0 LANGUAGES CXX)

# 默认安装前缀：项目根目录下的 local
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/../../local" CACHE PATH "Install prefix" FORCE)
endif()

# ----- 查找已安装的 cort 头文件 -----
find_path(CORT_INCLUDE_DIR cort PATHS ${CMAKE_INSTALL_PREFIX}/include NO_DEFAULT_PATH)
if(NOT CORT_INCLUDE_DIR)
    message(FATAL_ERROR "未找到 cort 头文件，请先编译安装 src/cort（路径：${CMAKE_INSTALL_PREFIX}/include）")
endif()

# ----- 查找已安装的 rpc 头文件 -----
find_path(RPC_INCLUDE_DIR rpc PATHS ${CMAKE_INSTALL_PREFIX}/include NO_DEFAULT_PATH)
if(NOT RPC_INCLUDE_DIR)
    message(FATAL_ERROR "未找到 rpc 头文件，请先编译安装 src/rpc（路径：${CMAKE_INSTALL_PREFIX}/include）")
endif()



# 收集源文件
file(GLOB CLERK_SRC "src/clerk/*.cpp")
file(GLOB CORE_SRC "src/core/*.cpp")
file(GLOB RPC_SRC "src/rpcProto/*.cc")

# 头文件目录
set(CLERK_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include/clerk)
set(CORE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include/core)
set(RPC_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include/rpcProto)

# 生成静态库
add_library(raft_rpcProto STATIC ${RPC_SRC})
add_library(raft_core     STATIC ${CORE_SRC})
add_library(raft_clerk    STATIC ${CLERK_SRC})

# 头文件搜索路径（供此编译使用）
target_include_directories(raft_rpcProto PRIVATE include)
target_include_directories(raft_core PRIVATE include ${CMAKE_INSTALL_PREFIX}/utils.h ${RPC_INCLUDE_DIR} ${CORT_INCLUDE_DIR})
target_include_directories(raft_clerk PRIVATE include ${CMAKE_INSTALL_PREFIX}/utils.h ${RPC_INCLUDE_DIR})

# 头文件搜索路径（供包含库的文件使用）
target_include_directories(raft_rpcProto INTERFACE $<INSTALL_INTERFACE:include>)
target_include_directories(raft_core INTERFACE $<INSTALL_INTERFACE:include>)
target_include_directories(raft_clerk INTERFACE $<INSTALL_INTERFACE:include>)

# 内部链接
target_link_libraries(raft_core PUBLIC raft_rpcProto)
target_link_libraries(raft_clerk PUBLIC raft_rpcProto)

# 安装头文件：将 include/ 下的所有 .h/.hpp 安装到 local/include/raft/ 下
install(DIRECTORY include/ DESTINATION include/raft FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

# 安装库文件到 local/lib/
install(TARGETS raft_rpcProto raft_core raft_clerk ARCHIVE DESTINATION lib)