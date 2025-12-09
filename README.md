# RISC-V 操作系统内核项目

## 📖 项目简介

本项目是在 RISC-V 架构上实现的简化操作系统内核教学项目。通过逐步完成多个实验，从零搭建一个可在 QEMU 上运行的内核，以此深入理解操作系统的关键概念与实现机制，包括启动流程、内存管理、进程与调度、系统调用以及简易文件系统等。

## 🎯 项目目标

- **学习目标**：理解操作系统内核的核心原理与典型实现路径  
- **技术目标**：熟悉 RISC-V 架构下的系统编程与内核级开发  
- **实践目标**：从零构建一个可启动、可交互的简化操作系统内核 

## 🚀 快速开始

### 环境要求

- **操作系统**: Linux/macOS/Windows (推荐使用 WSL2)
- **工具链**: RISC-V 交叉编译工具链
- **模拟器**: QEMU (支持 RISC-V)
- **构建工具**: Make

### 环境安装

#### 1. 安装 RISC-V 工具链

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install gcc-riscv64-unknown-elf qemu-system-misc
```

**macOS (使用 Homebrew):**
```bash
brew install riscv-tools qemu
```

**Windows (使用 WSL2):**
```bash
# 在 WSL2 中执行
sudo apt update
sudo apt install gcc-riscv64-unknown-elf qemu-system-misc
```

#### 2. 验证安装

```bash
# 检查工具链
riscv64-unknown-elf-gcc --version
qemu-system-riscv64 --version
```

### 编译和运行

#### 1. 克隆项目

```bash
git clone https://github.com/YKKing7/riscv-os.git
cd riscv-os
```

#### 2. 查看可用实验

```bash
# 列出当前支持的所有实验目录（exp1 / exp2 / ...）
make list
```

#### 3. 编译实验

```bash
# 编译指定实验（例如 exp1）
make build exp1
```

#### 4. 运行内核

```bash
# 依次运行所有已构建的实验，每个实验默认运行 5 秒后自动退出
make run

# 运行指定实验，直到手动退出（Ctrl + A, X 或直接关闭终端）
make run exp1
```

#### 5. 生成反汇编文件

```bash
# 为指定实验生成反汇编文件（.dump）
make dis exp1
```

#### 6. 清理构建文件

```bash
# 清理所有实验的构建产物
make clean

# 仅清理指定实验的构建产物
make clean exp1
```

## 📋 实验内容

本项目包含 9 个实验，逐步构建完整的操作系统内核：
- 实验0: 开发环境搭建
- 实验1: RISC-V引导与裸机启动
- 实验2: 内核printf与清屏功能实现
- 实验3: 页表与内存管理
- 实验4: 中断处理与时钟管理
- 实验5: 进程管理与调度
- 实验6: 系统调用
- 实验7: 文件系统
- 实验8: 系统扩展项目

## <u>🏗️ 项目架构</u>

本项目采用“多实验、多目录”的结构组织代码。以下以实验 7（相对完整的内核）为代表展示整体目录布局：

```
riscv-os/                      # RISC-V 多实验仓库
├── Makefile                   # 顶层构建
├── README.md                  # 项目说明
├── exp1                       # 实验 1
├── exp2                       # 实验 2
├── exp4                       # 实验 4
├── exp5                       # 实验 5
├── exp6                       # 实验 6
├── exp7                       # 综合内核实验
│   ├── boot                   # 启动
│   │   └── entry.S            # 汇编入口
│   ├── drivers                # 驱动
│   │   └── uart.c             # 串口
│   ├── fs                     # 文件系统
│   │   ├── bio.c              # 块缓存
│   │   ├── fs.c               # FS 核心
│   │   ├── log.c              # 日志
│   │   └── mkfs.c             # 制作镜像
│   ├── include                # 头文件
│   │   ├── buf.h
│   │   ├── fs.h
│   │   ├── memlayout.h
│   │   ├── pmm.h
│   │   ├── printf.h
│   │   ├── proc.h
│   │   ├── riscv.h
│   │   ├── syscall.h
│   │   ├── trap.h
│   │   ├── types.h
│   │   ├── uart.h
│   │   ├── user.h
│   │   └── vm.h
│   ├── lib                    # 内核库
│   │   └── printf.c           # printf 实现
│   ├── linker                 # 链接
│   │   └── kernel.ld          # 链接脚本
│   ├── main.c                 # 内核入口
│   ├── mm                     # 内存管理
│   │   ├── pmm.c              # 物理内存
│   │   └── vm.c               # 虚拟内存
│   ├── proc                   # 进程
│   │   ├── proc.c             # 进程管理
│   │   └── swtch.S            # 上下文切换
│   ├── syscall                # 系统调用
│   │   ├── syscall.c          # 分发
│   │   ├── sysfile.c          # 文件相关
│   │   └── sysproc.c          # 进程相关
│   ├── trap                   # 中断/异常
│   │   ├── kernelvec.S        # 向量入口
│   │   └── trap.c             # 处理逻辑
│   └── user                   # 用户态
│       └── usys.S             # syscall stub
├── exp8-1                     # 实验 8-1
├── exp8-4                     # 实验 8-4
├── exp8-5                     # 实验 8-5
└── report-md                  # Markdown 报告

```

## 🔧 调试方法

1. **使用 GDB 调试**:
```bash
# 启动 QEMU 调试模式
qemu-system-riscv64 -machine virt -nographic -bios none -kernel build/exp1/kernel.elf -s -S

# 在另一个终端中连接 GDB
gdb-multiarch build/exp1/kernel.elf
(gdb) target remote :1234
```

2. **查看反汇编代码**:
```bash
make dis exp1
cat build/exp1/kernel.dump
```

## 📚 学习资源

- [RISC-V 官方文档](https://riscv.org/technical/specifications/)
- [RISC-V 特权架构手册](https://riscv.github.io/riscv-isa-manual/snapshot/privileged/#_preface)
- [xv6 操作系统文档](https://pdos.csail.mit.edu/6.828/2025/xv6/book-riscv-rev5.pdf)
- [QEMU RISC-V 文档](https://qemu.readthedocs.io/en/v8.1.5/system/target-riscv.html)

---

⚠️ **注意**:本项目仅用于教学与学习目的，请勿用于生产环境。
