#!/bin/bash
# 清理脚本 - 清除所有构建产物

# 颜色定义
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

echo -e "${YELLOW}清理构建产物...${NC}"

# 删除build目录
if [ -d "build" ]; then
    rm -rf build
    echo -e "${GREEN}✓ 已删除 build/ 目录${NC}"
fi

# 删除bin目录
if [ -d "bin" ]; then
    rm -rf bin
    echo -e "${GREEN}✓ 已删除 bin/ 目录${NC}"
fi

# 删除根目录下的CMake文件（如果存在）
rm -f CMakeCache.txt cmake_install.cmake Makefile
rm -rf CMakeFiles

# 删除src目录下的构建文件
if [ -d "src/packet_header" ]; then
    rm -f src/packet_header/libpacket.a
fi

echo -e "${GREEN}✓ 清理完成！${NC}"

