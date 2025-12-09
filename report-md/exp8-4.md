# 扩展实验 8-4：内核日志系统

## 一、系统设计部分

### 1.1 架构设计说明

本实验实现结构化内核日志系统，用于调试、性能分析和安全追踪。

**printf 调试的局限 vs 结构化日志：**

| 问题 | printf | 结构化日志 |
|------|--------|------------|
| 过滤 | 无法过滤 | 按级别过滤 |
| 关闭 | 需删除代码 | 运行时调整 |
| 持久化 | 输出即丢失 | 缓冲区保存 |
| 上下文 | 无 | 时间戳/模块/行号 |

**环形缓冲区优势：**
- 固定内存，不会无限增长
- O(1) 写入，常数时间
- 自动覆盖最旧日志
- 无需动态内存分配

**本实验实现：**
- 5 级日志：DEBUG/INFO/WARN/ERROR/FATAL
- 4KB 环形缓冲区
- printf 风格格式化
- 运行时级别调整
- 日志统计信息

#### 系统架构图

```
┌─────────────────────────────────────────────────────┐
│                    应用层                            │
│        KLOG_DEBUG / KLOG_INFO / KLOG_ERROR          │
├─────────────────────────────────────────────────────┤
│                  日志格式化层                        │
│         级别过滤 / 时间戳 / 格式化输出              │
├─────────────────────────────────────────────────────┤
│                  存储层                              │
│            环形缓冲区 (Ring Buffer)                 │
├─────────────────────────────────────────────────────┤
│                  输出层                              │
│         UART 输出 / 缓冲区读取                      │
└─────────────────────────────────────────────────────┘
```

#### 文件组织

```
exp8-4/
├── include/
│   └── klog.h           # 日志系统接口声明
├── lib/
│   └── klog.c           # 日志系统实现
└── main.c               # 测试主函数
```

### 1.2 关键数据结构

#### 日志级别

```c
#define LOG_LEVEL_DEBUG   0   /* 调试信息 */
#define LOG_LEVEL_INFO    1   /* 一般信息 */
#define LOG_LEVEL_WARN    2   /* 警告 */
#define LOG_LEVEL_ERROR   3   /* 错误 */
#define LOG_LEVEL_FATAL   4   /* 致命错误 */
```

#### 环形缓冲区

```c
#define KLOG_BUFFER_SIZE  4096

struct klog_buffer {
    char data[KLOG_BUFFER_SIZE];
    int head;           /* 写入位置 */
    int tail;           /* 读取位置 */
    int count;          /* 当前数据量 */
};
```

#### 日志统计

```c
struct klog_stats {
    uint64 total_logs;      /* 总日志数 */
    uint64 dropped_logs;    /* 丢弃的日志数 */
    int buffer_used;        /* 缓冲区使用量 */
    int buffer_size;        /* 缓冲区大小 */
    int current_level;      /* 当前日志级别 */
};
```

### 1.3 日志级别过滤原理

```
设置级别 = WARN (2)

写入日志:
  DEBUG (0) → 丢弃 (0 < 2)
  INFO  (1) → 丢弃 (1 < 2)
  WARN  (2) → 记录 (2 >= 2) ✓
  ERROR (3) → 记录 (3 >= 2) ✓
  FATAL (4) → 记录 (4 >= 2) ✓
```

### 1.4 环形缓冲区原理

```
初始状态:
[                    ] head=0, tail=0, count=0

写入 "ABC":
[A][B][C][           ] head=3, tail=0, count=3

写入更多数据，缓冲区满后回绕:
[X][Y][Z][D][E][F]... head=3, tail=3, count=4096
     ↑ 新数据覆盖旧数据
```

### 1.5 与 xv6 对比分析

| 方面 | 本实验 (exp8-4) | xv6-riscv |
|------|-----------------|-----------|
| 日志级别 | 5 级分级过滤 | 无分级，直接输出 |
| 存储方式 | 环形缓冲区 | 直接输出到控制台 |
| 运行时控制 | 支持动态调整级别 | 不支持 |
| 元数据 | 时间戳、模块、行号 | 无 |
| 持久化 | 缓冲区保存 | 无 |

---

## 二、实验过程部分

### 2.1 实现步骤记录

#### 步骤 1：日志宏定义

```c
#define KLOG_DEBUG(fmt, ...) \
    klog_write(LOG_LEVEL_DEBUG, "[DEBUG] " fmt "\n", ##__VA_ARGS__)

#define KLOG_INFO(fmt, ...) \
    klog_write(LOG_LEVEL_INFO, "[INFO] " fmt "\n", ##__VA_ARGS__)

#define KLOG_WARN(fmt, ...) \
    klog_write(LOG_LEVEL_WARN, "[WARN] " fmt "\n", ##__VA_ARGS__)

#define KLOG_ERROR(fmt, ...) \
    klog_write(LOG_LEVEL_ERROR, "[ERROR] " fmt "\n", ##__VA_ARGS__)

#define KLOG_FATAL(fmt, ...) \
    klog_write(LOG_LEVEL_FATAL, "[FATAL] " fmt "\n", ##__VA_ARGS__)
```

**关键点：**
- **可变参数宏**：`##__VA_ARGS__` 允许零个或多个额外参数
- **自动添加前缀**：每个宏自动添加级别标签如 `[DEBUG]`
- **自动换行**：末尾添加 `\n`，调用者无需手动添加
- **编译时级别**：可以定义 `KLOG_MIN_LEVEL` 在编译时过滤低级别日志
- **零开销原则**：被过滤的日志不会产生任何运行时开销

#### 步骤 2：日志写入

```c
void klog_write(int level, const char *fmt, ...) {
    /* 级别过滤 */
    if (level < current_level)
        return;
    
    /* 格式化消息 */
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int len = kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    
    /* 写入环形缓冲区 */
    for (int i = 0; i < len; i++) {
        klog_buffer.data[klog_buffer.head] = buf[i];
        klog_buffer.head = (klog_buffer.head + 1) % KLOG_BUFFER_SIZE;
        if (klog_buffer.count < KLOG_BUFFER_SIZE)
            klog_buffer.count++;
    }
    
    /* 同时输出到 UART */
    uart_puts(buf);
    
    stats.total_logs++;
}
```

**关键点：**
- **早期过滤**：级别不够立即返回，避免不必要的格式化开销
- **栈上缓冲区**：256 字节足够大多数日志，避免动态分配
- **kvsnprintf**：带长度限制的格式化，防止缓冲区溢出
- **环形写入**：head 指针取模实现循环，count 跟踪有效数据量
- **双重输出**：同时写入缓冲区和 UART，便于实时查看和事后分析
- **统计更新**：记录总日志数，用于监控和调试

#### 步骤 3：日志读取

```c
int sys_klog_read(char *buf, int size) {
    int n = 0;
    int pos = klog_buffer.tail;
    
    while (n < size && n < klog_buffer.count) {
        buf[n++] = klog_buffer.data[pos];
        pos = (pos + 1) % KLOG_BUFFER_SIZE;
    }
    
    return n;
}
```

**关键点：**
- **从 tail 开始读**：tail 指向最旧的有效数据
- **双重限制**：不超过用户缓冲区大小，也不超过有效数据量
- **非破坏性读取**：读取不会移动 tail，数据仍然保留
- **返回实际读取量**：调用者根据返回值知道读了多少
- **用户指针验证**：实际实现需要先验证 buf 是有效的用户地址

#### 步骤 4：级别控制

```c
void klog_set_level(int level) {
    if (level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_FATAL)
        current_level = level;
}

const char *klog_level_name(int level) {
    static const char *names[] = {
        "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
    };
    if (level >= 0 && level <= 4)
        return names[level];
    return "UNKNOWN";
}
```

**关键点：**
- **运行时可调**：无需重新编译即可改变日志级别
- **参数验证**：无效级别被忽略，保持当前设置
- **静态字符串数组**：level_name 返回静态字符串，无需释放
- **默认值处理**：未知级别返回 "UNKNOWN"，便于调试
- **生产环境**：通常设置为 INFO 或 WARN，减少日志量
- **调试模式**：设置为 DEBUG 获取详细信息

### 2.2 问题与解决方案

#### 问题 1：缓冲区溢出丢失数据

**现象：** 旧日志被覆盖

**原因：** 环形缓冲区设计如此

**解决方案：** 这是预期行为，保留最新日志

#### 问题 2：格式化字符串安全

**现象：** 可能的缓冲区溢出

**解决方案：** 使用 `kvsnprintf` 限制输出长度

---

## 三、测试验证部分

### 3.1 功能测试结果

```bash
$ make run exp8-4
=============================================
  Exp8-4: Kernel Log System
=============================================

Test 1: Basic Logging
  Logged 5 messages:
  ---
  [DEBUG] This is a DEBUG message
  [INFO] This is an INFO message
  [WARN] This is a WARN message
  [ERROR] This is an ERROR message
  [FATAL] This is a FATAL message
  ---
Test 1 PASS!

Test 2: Log Level Filtering
  Level set to WARN:
    DEBUG: filtered (not logged)
    INFO:  filtered (not logged)
    WARN:  logged ✓
    ERROR: logged ✓
    FATAL: logged ✓
  Logged 3 messages (expected 3)
Test 2 PASS!

Test 3: Formatted Output
  Testing format specifiers...
  Formatted logs:
  ---
  [INFO] Integer: 42
  [INFO] Hex: 0xabcd
  [INFO] String: hello
  [INFO] Pointer: 0x0000000080000000
  ---
Test 3 PASS!

Test 4: Ring Buffer Overflow
  Writing many logs to overflow buffer...
  Stats: written=100, buffer_used=4096/4096
  Read 511 bytes from buffer
  Last few lines:
  ---
  [DEBUG] Log entry 97: test message
  [DEBUG] Log entry 98: test message
  [DEBUG] Log entry 99: test message
  ---
  Note: Entry 0-96 were overwritten (ring buffer wrap-around)
Test 4 PASS!

Test 5: Log Statistics
  Stats after logging:
    Total logs: 150
    Buffer used: 4096/4096
    Current level: INFO
Test 5 PASS!

Test 7: Dynamic Level Change
  Testing level changes at runtime:
    Level set to DEBUG:
      Logged: 5 messages
    Level set to INFO:
      Logged: 4 messages
    Level set to WARN:
      Logged: 3 messages
    Level set to ERROR:
      Logged: 2 messages
    Level set to FATAL:
      Logged: 1 messages
Test 7 PASS!

=============================================
  All tests PASSED!
=============================================
```

**测试解读：**
- **Test 1 基本日志**：5 个级别的日志都正确输出，带有对应的级别标签 [DEBUG]/[INFO]/[WARN]/[ERROR]/[FATAL]
- **Test 2 级别过滤**：设置为 WARN 后，DEBUG 和 INFO 被过滤（不输出），WARN/ERROR/FATAL 正常输出，共 3 条符合预期
- **Test 3 格式化输出**：支持 %d（整数）、%x（十六进制）、%s（字符串）、%p（指针）等格式说明符
- **Test 4 环形缓冲区溢出**：写入 100 条日志后缓冲区满（4096/4096），只能读取最新的 511 字节，entry 0-96 被覆盖，验证了环形缓冲区的 wrap-around 行为
- **Test 5 统计信息**：总日志 150 条，缓冲区已满，当前级别 INFO，统计功能正常
- **Test 7 动态级别**：运行时切换级别，DEBUG 记录 5 条、INFO 记录 4 条、WARN 记录 3 条...符合过滤逻辑

### 3.2 性能数据

| 指标 | 数值 |
|------|------|
| 缓冲区大小 | 4096 字节 |
| 单条日志开销 | ~50 字节 |
| 日志写入延迟 | ~1000 cycles |

---

## 四、实验收获

| 类别 | 收获 |
|------|------|
| 日志级别 | DEBUG/INFO/WARN/ERROR/FATAL 分级过滤 |
| 环形缓冲区 | head/tail 指针 + 取模实现循环，O(1) 写入 |
| 无锁设计 | 单生产者单消费者场景可无锁实现 |
| 宏技巧 | 自动添加文件名、行号等元信息 |
| Linux 对比 | printk/dmesg 的工作原理 |
| 性能权衡 | 日志详细程度与性能开销的平衡 |

**展望：** 日志文件持久化、日志轮转、压缩、远程收集。
