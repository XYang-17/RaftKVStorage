cmake_minimum_required(VERSION 3.10)
project(CortExamples)

# 可执行文件输出目录 /bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# ----- 查找已安装的 cort 库和头文件 -----
set(LOCAL_DIR ${CMAKE_SOURCE_DIR}/../../local)

find_library(CORT_LIB cort PATHS ${LOCAL_DIR}/lib NO_DEFAULT_PATH)
if(NOT CORT_LIB)
    message(FATAL_ERROR "未找到 cort 库，请先编译安装 src/cort（路径：${LOCAL_DIR}/lib）")
endif()

find_path(CORT_INCLUDE_DIR cort PATHS ${LOCAL_DIR}/include NO_DEFAULT_PATH)
if(NOT CORT_INCLUDE_DIR)
    message(FATAL_ERROR "未找到 cort 头文件，请先编译安装 src/cort（路径：${LOCAL_DIR}/include）")
endif()

# 生成debug版本，可以进行gdb调试
# set(CMAKE_BUILD_TYPE "Debug")

# 生成release版本
set(CMAKE_BUILD_TYPE "Release")

# 添加头文件和库文件搜索路径
include_directories(${LOCAL_DIR}/include)
link_directories(${LOCAL_DIR}/lib)

# 收集测试源文件
file(GLOB TEST_SOURCES "*.c" "*.cpp")

# 生成可执行文件
foreach(TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)

    add_executable(${TEST_NAME} ${TEST_SOURCE})

    target_link_libraries(${TEST_NAME} cort muduo_base)
endforeach()