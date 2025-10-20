cmake_minimum_required(VERSION 3.10)
project(user_sim_screen VERSION 1.0.0 LANGUAGES C)

# 设置C标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 设置编译类型（默认Debug，可通过-DCMAKE_BUILD_TYPE=Release修改）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# 编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# 添加库目录
link_directories(${CMAKE_SOURCE_DIR}/libs)

# 包含头文件目录
include_directories(${CMAKE_SOURCE_DIR}/src/packet_header)

# 添加 packet_header 子目录（生成静态库）
add_subdirectory(src/packet_header)

# 创建主可执行文件
add_executable(user_sim_screen src/user_sim_screen.c)

# 链接库
target_link_libraries(user_sim_screen 
    PRIVATE 
    packet
    ${CMAKE_SOURCE_DIR}/libs/libnanomsg.a
    anl
    pthread
    dl
    m
)
