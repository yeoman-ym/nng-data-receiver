#!/bin/bash
# 构建脚本 - 自动创建build目录并编译

set -e  # 遇到错误立即退出

# 颜色定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}================================${NC}"
echo -e "${BLUE}  user_sim_screen 构建脚本${NC}"
echo -e "${BLUE}================================${NC}"

# 创建并进入build目录
echo -e "\n${GREEN}[1/3]${NC} 创建构建目录..."
mkdir -p build
cd build

# 运行CMake
echo -e "${GREEN}[2/3]${NC} 配置项目..."
cmake .. "$@"

# 编译
echo -e "${GREEN}[3/3]${NC} 编译项目..."
make -j$(nproc)

echo -e "\n${GREEN}✓ 编译成功！${NC}"
echo -e "可执行文件位于: ${BLUE}bin/user_sim_screen${NC}"
echo -e "\n运行示例:"
echo -e "  ${BLUE}./bin/user_sim_screen 11112${NC}"
echo -e "  ${BLUE}./bin/user_sim_screen 127.0.0.1 11112${NC}"
echo -e "  ${BLUE}./bin/user_sim_screen 192.168.1.100 11112${NC}"

