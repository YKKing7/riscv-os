# 实验一：RISC-V 最小内核

## 一、系统设计部分

### 1.1 架构设计说明

本实验实现 RISC-V 架构上的最小操作系统内核，建立从裸机启动到 C 语言执行的完整流程。

**启动阶段的核心任务：**

| 任务 | 说明 |
|------|------|
| 入口设置 | 汇编代码 `_start` 作为程序入口 |
| 栈初始化 | 设置 `sp` 寄存器，C 函数调用的前提 |
| BSS 清零 | 未初始化全局变量清零 |
| UART 输出 | 通过 MMIO 访问串口，输出调试信息 |

**本实验实现：**
- 汇编入口 `entry.S`：设置栈、清零 BSS
- UART 驱动 `uart.c`：字符输出
- 链接脚本 `kernel.ld`：定义内存布局
- 主函数 `main.c`：输出 "Hello OS"

#### 系统架构图

```
┌─────────────────────────────────────────────────────┐
│                    应用层                            │
│                 kernel_main()                       │
├─────────────────────────────────────────────────────┤
│                    驱动层                            │
│              UART Driver (uart.c)                   │
├─────────────────────────────────────────────────────┤
│                   硬件抽象层                         │
│              启动代码 (entry.S)                      │
├─────────────────────────────────────────────────────┤
│                    硬件层                            │
│         QEMU virt 平台 (RISC-V 64-bit)              │
└─────────────────────────────────────────────────────┘
```

#### 启动流程

```
QEMU 加载 ELF → _start (entry.S) → 设置栈指针 → 清零 BSS → kernel_main (main.c) → uart_puts → WFI 循环
```

#### 文件组织

```
exp1/
├── boot/
│   └── entry.S          # 汇编启动代码，系统入口点
├── drivers/
│   └── uart.c           # UART 串口驱动实现
├── include/
│   └── uart.h           # UART 驱动头文件，寄存器定义
├── linker/
│   └── kernel.ld        # 链接脚本，定义内存布局
└── main.c               # 内核主函数
```

### 1.2 关键数据结构

本实验作为最小内核，数据结构较为简单，主要涉及：

#### UART 寄存器映射

```c
/* UART0 硬件基地址 */
#define UART0_BASE  0x10000000UL

/* 16550 UART 寄存器偏移 */
#define UART_THR    0   /* 发送保持寄存器 (写) */
#define UART_RBR    0   /* 接收缓冲寄存器 (读) */
#define UART_IER    1   /* 中断使能寄存器 */
#define UART_FCR    2   /* FIFO 控制寄存器 (写) */
#define UART_LCR    3   /* 线路控制寄存器 */
#define UART_LSR    5   /* 线路状态寄存器 */

/* LSR 寄存器位定义 */
#define UART_LSR_RX_READY   (1 << 0)  /* 接收数据就绪 */
#define UART_LSR_TX_IDLE    (1 << 5)  /* 发送保持寄存器空 */
```

#### 内存布局（由链接脚本定义）

| 段名 | 起始地址 | 内容 | 权限 |
|------|----------|------|------|
| .text | 0x80000000 | 代码段 | r-x |
| .rodata | .text 之后 | 只读数据 | r-- |
| .data | .rodata 之后 | 已初始化数据 | rw- |
| .bss | .data 之后 | 未初始化数据 | rw- |

#### 栈空间

```asm
.section .bss
.align 4
stack:
    .skip   4096        # 4KB 栈空间
stack_top:              # 栈顶（栈向下增长）
```

### 1.3 与 xv6 对比分析

| 方面 | 本实验 (exp1) | xv6-riscv |
|------|---------------|-----------|
| **启动模式** | 直接 M 模式运行 | M → S 模式切换 |
| **内存管理** | 无 | 页表、物理内存分配 |
| **进程管理** | 无 | 多进程、调度器 |
| **中断处理** | 无 | 完整中断/异常框架 |
| **UART 驱动** | 轮询模式 | 中断驱动 + 缓冲区 |
| **文件系统** | 无 | 简单文件系统 |
| **代码规模** | ~100 行 | ~10000 行 |

#### 主要差异分析

1. **启动流程简化**
   - xv6 需要从 M 模式切换到 S 模式，设置 PMP、中断委托等
   - 本实验直接在 M 模式运行，省略了特权级切换

2. **UART 实现**
   - xv6 使用中断驱动，有发送/接收缓冲区
   - 本实验使用轮询模式，简单但效率较低

3. **BSS 清零**
   - 两者都在启动时清零 BSS 段
   - 实现方式相似，都是循环写零

### 1.4 设计决策理由

| 决策 | 理由 |
|------|------|
| **使用 M 模式** | 简化启动流程，专注于理解基本概念 |
| **轮询式 UART** | 无需中断支持，实现简单直观 |
| **4KB 栈空间** | 对于简单内核足够，后续实验可扩展 |
| **16 字节栈对齐** | 符合 RISC-V ABI 要求 |
| **分离链接脚本** | 清晰定义内存布局，便于理解和修改 |
| **寄存器宏定义** | 提高代码可读性，便于移植 |

---

## 二、实验过程部分

### 2.1 实现步骤记录

#### 步骤 1：编写链接脚本 (kernel.ld)

```ld
OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS {
  text PT_LOAD FLAGS(5);   /* r-x */
  data PT_LOAD FLAGS(6);   /* rw- */
}

SECTIONS {
  . = 0x80000000;          /* QEMU virt 平台内核加载地址 */
  
  .text : { *(.text.entry) *(.text*) } :text
  .rodata : { *(.rodata*) } :text
  .data : { *(.data*) } :data
  .bss : { _sbss = .; *(.bss*) *(COMMON) _ebss = .; } :data
  
  _end = .;
}
```

**关键点：**
- `ENTRY(_start)` 指定入口点
- `.text.entry` 段放在最前面，确保 `_start` 在 0x80000000
- 定义 `_sbss` 和 `_ebss` 符号供启动代码使用

#### 步骤 2：编写启动汇编 (entry.S)

```asm
    .section .text.entry
    .global _start

_start:
    la      sp, stack_top       # 设置栈指针
    
    # 清零 BSS 段
    la      t0, _sbss
    la      t1, _ebss
    bgeu    t0, t1, 2f
1:  sd      zero, 0(t0)
    addi    t0, t0, 8
    bltu    t0, t1, 1b
2:
    call    kernel_main         # 跳转到 C 主函数

halt:
    wfi
    j       halt
```

**关键点：**
- 必须先设置栈指针，C 函数调用需要栈
- BSS 清零确保未初始化全局变量为 0
- `wfi` 指令让 CPU 进入低功耗等待状态

#### 步骤 3：实现 UART 驱动 (uart.c)

```c
#define UART_REG(reg) (*(volatile unsigned char *)(UART0_BASE + (reg)))

void uart_init(void) {
    UART_REG(UART_IER) = 0x00;  /* 禁用中断 */
    UART_REG(UART_FCR) = 0x07;  /* 启用 FIFO */
    UART_REG(UART_LCR) = 0x03;  /* 8N1 */
}

void uart_putc(char c) {
    while ((UART_REG(UART_LSR) & UART_LSR_TX_IDLE) == 0)
        ;
    UART_REG(UART_THR) = c;
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}
```

**关键点：**
- 使用 `volatile` 防止编译器优化掉对硬件寄存器的访问
- 发送前检查 LSR 寄存器确保发送缓冲区空闲
- 换行符 `\n` 前添加回车符 `\r` 以兼容终端

#### 步骤 4：编写内核主函数 (main.c)

```c
void kernel_main(void) {
    uart_init();
    uart_puts("Hello OS\n");
    
    while (1) {
        __asm__ volatile ("wfi");
    }
}
```

### 2.2 问题与解决方案

#### 问题 1：程序无输出

**现象：** QEMU 启动后终端无任何输出

**排查过程：**
1. 检查链接脚本，确认入口地址正确
2. 使用 `objdump -d` 查看生成的 ELF 文件
3. 发现 `.text.entry` 段未放在最前面

**解决方案：** 修改链接脚本，确保 `*(.text.entry)` 在 `.text` 段最前面

#### 问题 2：输出乱码

**现象：** 终端显示乱码字符

**排查过程：**
1. 检查 UART 基地址是否正确
2. 检查 LSR 寄存器偏移

**解决方案：** 确认 QEMU virt 平台 UART 地址为 `0x10000000`

#### 问题 3：链接警告 RWX 权限

**现象：** 链接时警告 `has a LOAD segment with RWX permissions`

**原因：** 默认情况下所有段合并，权限为 RWX

**解决方案：** 在链接脚本中使用 `PHDRS` 分离代码段和数据段权限

```ld
PHDRS {
  text PT_LOAD FLAGS(5);   /* r-x */
  data PT_LOAD FLAGS(6);   /* rw- */
}
```

### 2.3 源码理解总结

#### RISC-V 启动流程

1. **复位向量**：QEMU virt 平台从 0x80000000 开始执行
2. **栈初始化**：RISC-V 栈向下增长，SP 指向栈顶
3. **BSS 清零**：C 语言规范要求未初始化全局变量为 0
4. **函数调用**：使用 `call` 指令，自动保存返回地址到 `ra`

#### 16550 UART 工作原理

```
CPU → THR (发送保持寄存器) → 移位寄存器 → TX 引脚 → 终端
                ↑
        LSR.TX_IDLE 表示 THR 空闲
```

1. CPU 检查 LSR 寄存器的 TX_IDLE 位
2. 若为 1，表示可以发送，将字符写入 THR
3. UART 硬件自动将字符移位发送
4. 发送完成后 TX_IDLE 再次置 1

---

## 三、测试验证部分

### 3.1 功能测试结果

#### 编译测试

```bash
$ make build exp1
编译 exp1: exp1/drivers/uart.c
编译 exp1: exp1/main.c
汇编 exp1: exp1/boot/entry.S
链接 exp1...
构建完成: build/exp1/kernel.elf
```

✅ 编译无错误、无警告

#### 运行测试

```bash
$ make run exp1
启动 QEMU 运行 exp1...
Hello OS
```

✅ 正确输出 "Hello OS"

**测试解读：**
- **编译测试**：无错误无警告说明代码语法正确，链接脚本配置正确
- **运行测试**：成功输出 "Hello OS" 说明：
  - 入口点设置正确（从 0x80000000 开始执行）
  - 栈初始化正确（C 函数能正常调用）
  - BSS 段清零正确（全局变量初始化正常）
  - UART 驱动工作正常（字符能正确发送到终端）

#### ELF 文件验证

```bash
$ riscv64-unknown-elf-objdump -h build/exp1/kernel.elf

Sections:
Idx Name          Size      VMA               LMA
  0 .text         00000xxx  0000000080000000  0000000080000000
  1 .rodata       00000xxx  00000000800000xx  00000000800000xx
  2 .data         00000xxx  00000000800000xx  00000000800000xx
  3 .bss          00001xxx  00000000800000xx  00000000800000xx
```

✅ 各段地址正确，从 0x80000000 开始

### 3.2 性能数据

| 指标 | 数值 |
|------|------|
| 内核 ELF 大小 | ~2 KB |
| 代码段大小 | ~500 字节 |
| 启动时间 | < 1ms (QEMU) |
| 栈使用量 | < 100 字节 |

### 3.3 异常测试

#### 测试 1：移除 BSS 清零

**修改：** 注释掉 entry.S 中的 BSS 清零代码

**结果：** 本实验无影响（无全局变量依赖初始值）

**结论：** BSS 清零对于使用未初始化全局变量的程序至关重要

#### 测试 2：错误的栈地址

**修改：** 将 `la sp, stack_top` 改为 `li sp, 0`

**结果：** 程序崩溃，无输出

**结论：** 正确的栈初始化是 C 函数调用的前提

#### 测试 3：错误的 UART 地址

**修改：** 将 `UART0_BASE` 改为错误地址

**结果：** 无输出（写入无效地址）

**结论：** 硬件地址必须与平台匹配

---

## 四、实验收获

| 类别 | 收获 |
|------|------|
| 裸机编程 | 上电 → 汇编入口 → 栈初始化 → BSS 清零 → C 代码的完整流程 |
| 硬件交互 | 内存映射 I/O (MMIO) 访问 UART 寄存器 |
| 工具链 | 交叉编译、链接脚本、QEMU 调试 |
| 底层思维 | 理解操作系统最底层的运行机制 |

**展望：** 添加更多外设驱动、实现中断处理、支持多核启动。
