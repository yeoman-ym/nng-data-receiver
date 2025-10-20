cmake_minimum_required(VERSION 3.10)

# 生成静态库 libpacket.a
add_library(packet STATIC packet_header.c)

# 头文件路径，供主项目使用
target_include_directories(packet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
