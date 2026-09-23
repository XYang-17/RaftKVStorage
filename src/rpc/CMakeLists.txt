cmake_minimum_required(VERSION 3.10)
project(RpcLib CXX)

# 默认安装前缀：项目根目录下的 local
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/../../local" CACHE PATH "Install prefix" FORCE)
endif()

# 收集源文件
file(GLOB SOURCES "*.cpp" "*.c" "*.cc")

# 生成静态库
add_library(rpc STATIC ${SOURCES})

# 头文件搜索路径（供此编译使用）
target_include_directories(rpc PRIVATE include ${CMAKE_INSTALL_PREFIX}/include)

# 头文件搜索路径（供包含库的文件使用）
target_include_directories(rpc INTERFACE $<INSTALL_INTERFACE:include>)

# 安装库文件到 local/lib/
install(TARGETS rpc ARCHIVE DESTINATION lib)

# 安装头文件：将 include/ 下的所有 .h/.hpp 安装到 local/include/rpc/ 下
install(DIRECTORY include/
        DESTINATION include/rpc
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")