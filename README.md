# user_sim_screen

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![C](https://img.shields.io/badge/language-C99-orange.svg)]()

基于 nanomsg 的高性能数据接收与解析工具，用于实时接收并解析来自 TCP 地址的数据包，支持多种数据类型的解析与打印，适用于仿真监控等高性能场景。

## 特性

- 🚀 **高性能**：基于 nanomsg 实现的高效数据传输
- 📦 **多类型支持**：支持多种数据包类型的自动解析
- 🛡️ **稳定可靠**：内置信号处理机制，自动资源清理
- 🔍 **调试友好**：支持异常信号的堆栈回溯
- ⚡ **轻量级**：最小化依赖，易于集成

## 快速开始

### 依赖要求

- CMake >= 3.10
- GCC/Clang（支持C99标准）
- nanomsg 库（已包含静态库）

### 编译

```bash
# 创建构建目录并编译
mkdir -p build
cd build
cmake ..
make

# 可执行文件位于 bin/ 目录
cd ..
```

**快速编译脚本：**

```bash
# 使用提供的编译脚本
./build.sh
```

### 使用方法

```bash
./bin/user_sim_screen <nanomsg_url>
```

**参数说明：**
- `<nanomsg_url>`：监听的 nanomsg 地址，例如 `tcp://0.0.0.0:11112`

**示例：**

```bash
./bin/user_sim_screen tcp://0.0.0.0:11112
```

## 功能说明

### 核心功能

- **PULL模式**：程序作为 nanomsg 的 PULL 端，持续监听并接收数据
- **自动解析**：支持仿真状态、变量监控、调试信息等多种数据包类型
- **信号处理**：优雅处理 SIGTERM、SIGINT、SIGSEGV 等信号，防止内存泄漏
- **堆栈回溯**：异常时自动输出堆栈信息，便于调试定位

### 输出示例

```
[###SIMULATION_STARTED###]
VarMon DATA: uint64_t:123, double:456.789, recv_num:1
[###UNKNOWN type X###]
```

## 项目结构

```
nng_test/
├── CMakeLists.txt              # 主构建配置
├── README.md                   # 项目说明文档
├── LICENSE                     # MIT 开源协议
├── CONTRIBUTING.md             # 贡献指南
├── build.sh                    # 一键构建脚本
├── clean.sh                    # 清理脚本
├── src/                        # 源代码目录
│   ├── user_sim_screen.c       # 主程序源码
│   └── packet_header/          # 数据包解析模块
│       ├── CMakeLists.txt      # packet 模块构建配置
│       ├── packet_header.h     # 数据包头文件定义
│       └── packet_header.c     # 数据包解析实现
├── libs/                       # 依赖库
│   └── libnanomsg.a            # nanomsg 静态库
├── build/                      # 构建目录（CMake中间文件）
└── bin/                        # 可执行文件输出目录（构建后生成）
```

## 性能优化建议

### 多核优化

推荐在多核服务器上运行，结合 CPU 绑定工具提升性能：

```bash
# 绑定到特定 CPU 核心
taskset -c 0-3 ./bin/user_sim_screen tcp://0.0.0.0:11112

# NUMA 优化
numactl --cpunodebind=0 --membind=0 ./bin/user_sim_screen tcp://0.0.0.0:11112
```

### 清理构建

```bash
# 清理所有构建产物
./clean.sh
```

### 编译优化

```bash
# Release 模式编译（启用 -O3 优化）
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
cd ..
```

### 高级优化

- 关键路径可考虑使用 SIMD 指令优化
- 采用多线程并行处理提升吞吐量
- 使用零拷贝技术减少内存操作

## 注意事项

- ⚠️ 确保监听端口未被占用且网络可达
- ⚠️ 调试时可手动发送信号测试（如 `kill -TERM <pid>`）
- ⚠️ 修改监听地址时，注意防火墙配置

## 许可证

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE) 文件。

## 贡献

欢迎提交 Issue 和 Pull Request！

## 联系方式

如有问题或建议，请通过 GitHub Issues 联系。

---

**Happy Coding! 🎉**
