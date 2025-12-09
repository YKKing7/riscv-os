# 实验二：UART 驱动与 printf 实现

## 一、系统设计部分

### 1.1 架构设计说明

本实验实现格式化输出功能，`printf` 是操作系统开发中最基础的调试工具。裸机环境下无法使用标准库，需从零实现。

**核心挑战：**

| 挑战 | 说明 |
|------|------|
| 可变参数 | 使用 `<stdarg.h>` 宏提取不定数量参数 |
| 格式解析 | 解析 `%d`、`%s`、`%x` 等格式说明符 |
| 数值转换 | 整数到字符串（十进制/十六进制） |
| 终端控制 | ANSI 转义序列实现颜色和光标控制 |

**本实验实现：**
- 格式化输出 `printf`/`printf_color`
- 支持 8 种格式说明符
- ANSI 颜色和光标控制
- 边界条件处理（NULL、INT_MIN 等）

#### 系统架构图

```
┌─────────────────────────────────────────────────────┐
│                    应用层                            │
│                 kernel_main()                       │
├─────────────────────────────────────────────────────┤
│                   格式化输出层                       │
│           printf / printf_color (printf.c)          │
├─────────────────────────────────────────────────────┤
│                    驱动层                            │
│              UART Driver (uart.c)                   │
├─────────────────────────────────────────────────────┤
│                    硬件层                            │
│         QEMU virt 平台 (RISC-V 64-bit)              │
└─────────────────────────────────────────────────────┘
```

#### 文件组织

```
exp2/
├── boot/
│   └── entry.S          # 汇编启动代码
├── drivers/
│   └── uart.c           # UART 串口驱动
├── include/
│   ├── uart.h           # UART 驱动头文件
│   ├── types.h          # 基本数据类型定义
│   └── printf.h         # printf 函数声明
├── lib/
│   └── printf.c         # printf 格式化输出实现
├── linker/
│   └── kernel.ld        # 链接脚本
└── main.c               # 内核主函数
```

### 1.2 关键数据结构

#### 格式化输出支持的格式

| 格式符 | 说明 | 示例 |
|--------|------|------|
| `%d` | 有符号十进制整数 | `printf("%d", -42)` → `-42` |
| `%u` | 无符号十进制整数 | `printf("%u", 42)` → `42` |
| `%x` | 十六进制整数 | `printf("%x", 255)` → `ff` |
| `%ld` | 长整型 | `printf("%ld", 123456789L)` |
| `%p` | 指针地址 | `printf("%p", ptr)` → `0x80000000...` |
| `%s` | 字符串 | `printf("%s", "hello")` |
| `%c` | 单个字符 | `printf("%c", 'A')` |
| `%%` | 百分号 | `printf("%%")` → `%` |

#### ANSI 终端控制

```c
/* 颜色代码 */
#define COLOR_RED     31
#define COLOR_GREEN   32
#define COLOR_YELLOW  33
#define COLOR_BLUE    34
#define COLOR_CYAN    36

/* 光标控制 */
void goto_xy(int x, int y);   /* 移动光标到 (x, y) */
void clear_line(void);        /* 清除当前行 */
void clear_screen(void);      /* 清屏 */
```

### 1.3 与 xv6 对比分析

| 方面 | 本实验 (exp2) | xv6-riscv |
|------|---------------|-----------|
| **格式化输出** | 完整 printf 实现 | 简化版 printf |
| **颜色支持** | ANSI 颜色输出 | 无 |
| **光标控制** | 支持定位/清行 | 无 |
| **长整型** | 支持 %ld/%lu | 支持 |
| **NULL 处理** | 输出 "(null)" | 可能崩溃 |

### 1.4 设计决策理由

| 决策 | 理由 |
|------|------|
| **可变参数** | 使用 `<stdarg.h>` 标准库实现参数提取，这是 C 语言标准的一部分，具有良好的可移植性 |
| **数字转换** | 使用缓冲区逆序存储，支持任意进制。这种方法避免了递归调用，栈使用量可预测 |
| **ANSI 转义** | 兼容标准终端（如 xterm、VT100），无需特殊硬件支持，广泛适用于各种终端模拟器 |
| **NULL 安全** | 输出 "(null)" 而非崩溃，提高健壮性。这与 glibc 的行为一致，便于调试 |
| **分层设计** | 将字符输出（`consputc`）与格式化逻辑分离，便于移植到不同输出设备 |

### 1.5 技术要点深入分析

#### 可变参数机制原理

C 语言的可变参数基于栈传递约定。在 RISC-V 上，前 8 个参数通过寄存器 `a0-a7` 传递，超出部分通过栈传递。`va_list` 实际上是一个指针，指向参数列表在内存中的位置。

```c
void example(int count, ...) {
    va_list ap;
    va_start(ap, count);  // ap 指向 count 之后的第一个参数
    
    for (int i = 0; i < count; i++) {
        int val = va_arg(ap, int);  // 提取一个 int，ap 后移
    }
    
    va_end(ap);  // 清理
}
```

#### 整数到字符串转换算法

将整数转换为字符串的核心是反复除以进制数，取余数作为当前位的数字。由于这个过程产生的是逆序结果，我们使用缓冲区暂存，最后反向输出。

```
例：将 123 转换为十进制字符串
123 % 10 = 3  →  buf[0] = '3'
12  % 10 = 2  →  buf[1] = '2'
1   % 10 = 1  →  buf[2] = '1'
反向输出: "123"
```

---

## 二、实验过程部分

### 2.1 实现步骤记录

#### 步骤 1：定义基本类型 (types.h)

```c
typedef unsigned char      uint8;
typedef unsigned short     uint16;
typedef unsigned int       uint32;
typedef unsigned long long uint64;
typedef long long          int64;
```

**关键点：**
- 使用固定宽度类型，避免不同平台上 `int`、`long` 大小不一致的问题
- RISC-V 64 位平台上，`long` 是 64 位，但为了可移植性仍使用 `uint64`
- 这些类型定义是后续所有实验的基础

#### 步骤 2：实现整数打印 (printf.c)

```c
static void printint(int64 xx, int base, int sign) {
    char buf[24];
    int i = 0;
    uint64 x;

    if (sign && xx < 0) {
        x = -xx;
        sign = 1;
    } else {
        x = xx;
        sign = 0;
    }

    do {
        buf[i++] = digits[x % base];
        x /= base;
    } while (x != 0);

    if (sign)
        consputc('-');

    while (--i >= 0)
        consputc(buf[i]);
}
```

**关键点：**
- **缓冲区逆序存储**：除法取余得到的是最低位，所以先存储低位，最后反向输出
- **负数处理**：先记录符号，转为正数处理，最后输出负号。注意 `INT_MIN` 取负会溢出，需要用 `uint64` 存储
- **do-while 循环**：保证至少输出一个字符（处理 0 的情况）
- **24 字节缓冲区**：足够存储 64 位整数的十进制表示（最多 20 位）加符号

#### 步骤 3：实现格式解析

```c
static void vprintf_internal(const char *fmt, va_list ap) {
    for (int i = 0; (c = fmt[i]) != 0; i++) {
        if (c != '%') {
            consputc(c);
            continue;
        }
        c = fmt[++i];
        switch (c) {
        case 'd': printint(va_arg(ap, int), 10, 1); break;
        case 'x': printint(va_arg(ap, uint32), 16, 0); break;
        case 's': /* 字符串处理 */ break;
        case 'p': printptr(va_arg(ap, uint64)); break;
        /* ... */
        }
    }
}
```

**关键点：**
- **状态机解析**：遇到 `%` 进入格式解析状态，否则直接输出字符
- **va_arg 类型匹配**：必须与实际传入的参数类型匹配，否则会读取错误的数据
- **整数提升**：`char` 和 `short` 会被提升为 `int`，所以 `%c` 使用 `va_arg(ap, int)`
- **格式字符串结束检查**：`%` 后面可能是字符串结尾，需要检查避免越界

#### 步骤 4：实现颜色输出

```c
int printf_color(int color, const char *fmt, ...) {
    set_color(color);      /* 发送 ESC[xxm */
    vprintf_internal(fmt, ap);
    reset_color();         /* 发送 ESC[0m */
    return 0;
}
```

**关键点：**
- **ANSI 转义序列**：`ESC[` 开始，`m` 结束，中间是颜色代码（如 31=红色）
- **配对使用**：`set_color` 和 `reset_color` 必须配对，否则后续输出都会带颜色
- **终端兼容性**：ANSI 转义序列被大多数终端支持，但在不支持的终端上会显示乱码
- **嵌套调用**：如果 `vprintf_internal` 中也调用了颜色输出，会导致颜色混乱

### 2.2 问题与解决方案

#### 问题 1：负数输出错误

**现象：** `-123` 输出为很大的正数

**原因：** 未正确处理符号位

**解决方案：** 在 `printint` 中先判断符号，转为正数后处理

#### 问题 2：指针输出不完整

**现象：** 64 位指针只显示低 32 位

**原因：** 使用 `%x` 格式，只取 32 位参数

**解决方案：** 实现专用的 `printptr` 函数，固定输出 16 位十六进制

### 2.3 源码理解总结

#### 可变参数机制

```c
va_list ap;
va_start(ap, fmt);    /* 初始化参数列表 */
int val = va_arg(ap, int);  /* 提取下一个参数 */
va_end(ap);           /* 清理 */
```

#### ANSI 转义序列

```
ESC[31m  → 设置红色前景
ESC[0m   → 重置所有属性
ESC[2J   → 清屏
ESC[y;xH → 光标移动到 (x, y)
```

---

## 三、测试验证部分

### 3.1 功能测试结果

#### 运行测试

```bash
$ make run exp2
=============================================
  Exp2: UART Driver & printf Implementation
=============================================

Test 1: printf basic functions
  Integer:  42
  Negative: -123
  Hex:      0xabcd
  String:   Hello RISC-V
  Char:     X
  Pointer:  0x0000000080000000
  Percent:  %
Test 1 PASS!

Test 2: printf boundary cases
  INT_MAX:     2147483647
  INT_MIN:     -2147483648
  UINT_MAX:    4294967295
  Long:        123456789012345
  NULL string: (null)
Test 2 PASS!

Test 3: Color output
  Red text      (红色)
  Green text    (绿色)
  Yellow text   (黄色)
  Blue text     (蓝色)
  Cyan text     (青色)
Test 3 PASS!

Test 4: Cursor positioning
Test 4 PASS!

Test 5: Clear line
Test 5 PASS!

=============================================
  All tests passed!
=============================================
```

**测试解读：**
- **Test 1 基本功能**：验证了整数、负数、十六进制、字符串、字符、指针等基本格式的正确输出，说明格式解析和数值转换逻辑正确
- **Test 2 边界情况**：INT_MAX/INT_MIN 正确输出说明符号处理和溢出处理正确；NULL 字符串输出 `(null)` 说明防御性编程到位
- **Test 3 颜色输出**：各种颜色正确显示说明 ANSI 转义序列生成正确，终端能正确解析
- **Test 4/5 光标控制**：光标定位和清行功能正常，说明终端控制字符序列实现正确

### 3.2 性能数据

| 指标 | 数值 |
|------|------|
| printf.c 代码量 | ~200 行 |
| 支持格式数 | 8 种 |
| 单字符输出延迟 | ~100 cycles |

---

## 四、实验收获

| 类别 | 收获 |
|------|------|
| 可变参数 | va_list/va_start/va_arg/va_end 与 RISC-V 调用约定 |
| 数字格式化 | 整数→字符串转换，负数和进制处理 |
| 终端控制 | ANSI/VT100 转义序列实现颜色和光标控制 |
| 边界处理 | NULL 指针、INT_MIN 溢出、零值等特殊情况 |
| 工程价值 | printf 为后续所有实验提供基础调试能力 |

**展望：** 可扩展中断驱动异步输出、浮点数格式、sprintf 等功能。
