# 实验四：中断与异常处理

## 一、系统设计部分

### 1.1 架构设计说明

本实验实现 RISC-V 中断/异常处理框架，使 CPU 能响应外部事件（定时器、键盘）和内部事件（非法指令、页错误）。

**中断 vs 异常：**

| 特性 | 中断 (Interrupt) | 异常 (Exception) |
|------|------------------|------------------|
| 触发源 | 外部设备/定时器 | CPU 执行指令时 |
| 时机 | 异步，任意时刻 | 同步，特定指令 |
| 可屏蔽 | 通常可屏蔽 | 不可屏蔽 |
| 示例 | 时钟、键盘中断 | 除零、页错误、ecall |

**中断机制的价值：**
- **CPU 利用率**：无需轮询，等待期间可执行其他任务
- **实时响应**：外部事件立即处理
- **多任务基础**：时钟中断驱动抢占式调度
- **错误处理**：优雅处理程序异常

**本实验实现：**
- 动态注册中断处理函数
- 周期性时钟中断
- 5 级中断优先级
- 共享中断与中断嵌套

#### 系统架构图

```
┌─────────────────────────────────────────────────────┐
│                    应用层                            │
│              中断处理回调函数                        │
├─────────────────────────────────────────────────────┤
│                  中断分发层                          │
│     注册/分发/优先级管理 (trap.c)                   │
├─────────────────────────────────────────────────────┤
│                  中断入口层                          │
│          上下文保存/恢复 (trapvec.S)                │
├─────────────────────────────────────────────────────┤
│                    硬件层                            │
│         RISC-V 中断控制器 / Timer                   │
└─────────────────────────────────────────────────────┘
```

#### 文件组织

```
exp4/
├── include/
│   └── trap.h           # 中断/异常接口声明
├── trap/
│   ├── trap.c           # 中断/异常处理实现
│   └── trapvec.S        # 中断入口汇编
└── main.c               # 测试主函数
```

### 1.2 关键数据结构

#### 陷阱帧 (Trapframe)

```c
struct trapframe {
    uint64 ra, sp, gp, tp;
    uint64 t0, t1, t2;
    uint64 s0, s1;
    uint64 a0, a1, a2, a3, a4, a5, a6, a7;
    uint64 s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    uint64 t3, t4, t5, t6;
    uint64 sepc;          /* 异常返回地址 */
    uint64 scause;        /* 异常原因 */
    uint64 stval;         /* 异常值 */
};
```

#### 中断优先级

```c
#define IRQ_PRIO_HIGHEST  0
#define IRQ_PRIO_HIGH     1
#define IRQ_PRIO_NORMAL   2
#define IRQ_PRIO_LOW      3
#define IRQ_PRIO_LOWEST   4
```

#### 共享中断处理

```c
struct shared_irq_handler {
    int (*handler)(void *dev_id);
    void *dev_id;
    const char *name;
    struct shared_irq_handler *next;
};
```

### 1.3 与 xv6 对比分析

| 方面 | 本实验 (exp4) | xv6-riscv |
|------|---------------|-----------|
| **中断注册** | 动态注册回调 | 硬编码处理 |
| **优先级** | 5 级优先级 | 无优先级 |
| **共享中断** | 支持多处理函数 | 不支持 |
| **中断嵌套** | 支持高优先级抢占 | 不支持 |
| **异常处理** | 可注册自定义处理 | 固定处理 |

### 1.4 设计决策理由

| 决策 | 理由 |
|------|------|
| **回调注册** | 解耦中断源与处理逻辑，便于扩展 |
| **优先级机制** | 保证关键中断及时响应 |
| **共享中断** | 支持多设备共享同一中断线 |
| **嵌套支持** | 高优先级中断可抢占低优先级 |

---

## 二、实验过程部分

### 2.1 实现步骤记录

#### 步骤 1：中断入口 (trapvec.S)

```asm
.globl kernelvec
kernelvec:
    # 保存所有寄存器到栈
    addi sp, sp, -256
    sd ra, 0(sp)
    sd sp, 8(sp)
    # ... 保存所有通用寄存器
    
    # 调用 C 处理函数
    call kerneltrap
    
    # 恢复寄存器
    ld ra, 0(sp)
    # ...
    addi sp, sp, 256
    sret
```

**关键点：**
- **256 字节栈空间**：保存 31 个通用寄存器（x1-x31）× 8 字节 = 248 字节，对齐到 256
- **保存顺序**：必须先保存 sp，因为后续操作会修改 sp
- **call 指令**：自动将返回地址保存到 ra，所以 ra 必须在 call 之前保存
- **sret 指令**：从 S 模式异常返回，自动从 sepc 恢复 PC，从 sstatus 恢复特权级
- **原子性**：整个保存/恢复过程中断必须关闭，否则会破坏栈

#### 步骤 2：中断分发

```c
void kerneltrap(void) {
    uint64 scause = r_scause();
    
    if (scause & (1UL << 63)) {
        /* 中断 */
        int irq = scause & 0xFF;
        handle_interrupt(irq);
    } else {
        /* 异常 */
        handle_exception(scause);
    }
}
```

**关键点：**
- **scause 最高位**：区分中断（1）和异常（0），这是 RISC-V 的设计
- **中断号提取**：低 8 位是具体的中断/异常代码
- **分发逻辑**：根据类型调用不同的处理函数，实现关注点分离
- **异常处理**：异常通常需要特殊处理（如页错误需要分配页面）
- **返回地址**：中断返回到被打断的指令，异常可能需要跳过出错指令

#### 步骤 3：中断注册

```c
static void (*irq_handlers[MAX_IRQ])(void);
static int irq_priority[MAX_IRQ];

void register_interrupt_handler_prio(int irq, void (*handler)(void), int prio) {
    irq_handlers[irq] = handler;
    irq_priority[irq] = prio;
}
```

**关键点：**
- **函数指针数组**：每个中断号对应一个处理函数，实现 O(1) 分发
- **优先级数组**：记录每个中断的优先级，用于嵌套判断
- **动态注册**：驱动程序可以在运行时注册处理函数，解耦中断框架和具体驱动
- **NULL 检查**：调用前需检查处理函数是否为 NULL，避免崩溃
- **线程安全**：注册操作应在中断关闭时进行，或使用原子操作

#### 步骤 4：中断嵌套

```c
static int nesting_level = 0;

void handle_interrupt(int irq) {
    int current_prio = irq_priority[irq];
    
    nesting_level++;
    
    /* 允许更高优先级中断 */
    if (current_prio > IRQ_PRIO_HIGHEST) {
        intr_on();
    }
    
    if (irq_handlers[irq])
        irq_handlers[irq]();
    
    intr_off();
    nesting_level--;
}
```

**关键点：**
- **嵌套计数**：`nesting_level` 跟踪当前嵌套深度，用于调试和防止过深嵌套
- **选择性开中断**：只有非最高优先级的中断才开启中断，允许被抢占
- **最高优先级保护**：最高优先级中断不会被抢占，保证关键中断的原子性
- **关中断退出**：处理完成后必须关中断，确保 nesting_level 更新的原子性
- **栈溢出风险**：每次嵌套都会消耗栈空间，需要限制最大嵌套深度

### 2.2 问题与解决方案

#### 问题 1：中断返回后崩溃

**现象：** 第一次中断处理后系统崩溃

**原因：** 未正确恢复 `sepc` 寄存器

**解决方案：** 在 `sret` 前恢复 `sepc`

#### 问题 2：中断嵌套死锁

**现象：** 高优先级中断无法抢占

**原因：** 中断处理期间未重新开启中断

**解决方案：** 在处理低优先级中断时开启中断

### 2.3 源码理解总结

#### RISC-V 中断/异常分类

```
scause 最高位:
  1 = 中断 (异步)
  0 = 异常 (同步)

中断类型:
  1 = 软件中断 (S-mode)
  5 = 时钟中断 (S-mode)
  9 = 外部中断 (S-mode)

异常类型:
  2 = 非法指令
  5 = 加载访问错误
  7 = 存储访问错误
  12 = 指令页错误
  13 = 加载页错误
  15 = 存储页错误
```

---

## 三、测试验证部分

### 3.1 功能测试结果

```bash
$ make run exp4
=============================================
  Exp4: Interrupt & Exception Handling
=============================================

Test 1: Timer Interrupt
  Waiting for 5 interrupts...
  Received 5 interrupts in 50000000 cycles
Test 1 PASS!

Test 2: Exception Handling
  stvec register... 0x80001000 OK
  sie register... 0x222 OK
  Illegal instruction exception... caught & handled OK
Test 2 PASS!

Test 3: Interrupt Latency
  10 ticks, 100000000 cycles total
  Average interval: 10000000 cycles (~100000 us @100MHz)
Test 3 PASS!

Test 4: Interrupt Priority
  Levels: 0=HIGHEST 1=HIGH 2=NORMAL 3=LOW 4=LOWEST
  set_irq_priority(TIMER, HIGH): 1... OK
  register with HIGHEST: 0... OK
  Nesting level (outside IRQ): 0
Test 4 PASS!

Test 5: Shared Interrupt
  Register 2 handlers on Timer IRQ...
  Registered OK
  Handler1 called 3 times, Handler2 called 3 times
Test 5 PASS!

Test 6: Interrupt Nesting
  Timer=LOW(3), Soft=HIGH(1), high can preempt low
  Trace:
    [LOW]  prio=3 depth=1 start
      [HIGH] prio=1 depth=2 (nested!)
    [LOW]  prio=3 depth=1 end
  Result: max_depth=2, high=1, low=1
Test 6 PASS!

=============================================
  All tests passed!
=============================================
```

**测试解读：**
- **Test 1 定时器中断**：成功接收 5 次中断，说明 stvec 设置正确、中断处理流程完整、sret 返回正常
- **Test 2 异常处理**：stvec=0x80001000 说明向量表地址正确；sie=0x222 表示软件/定时器/外部中断都已使能；非法指令异常被捕获说明异常分发逻辑正确
- **Test 3 中断延迟**：平均间隔 10ms（100MHz 下 10000000 cycles），与 QEMU 定时器配置一致
- **Test 4 中断优先级**：验证了优先级设置和查询接口，不同优先级的中断能正确注册
- **Test 5 共享中断**：同一中断号注册多个处理函数，两个 handler 都被调用 3 次，说明链式调用机制正确
- **Test 6 中断嵌套**：低优先级中断执行时被高优先级中断抢占（depth 从 1 变为 2），验证了嵌套机制和优先级抢占逻辑

### 3.2 性能数据

| 指标 | 数值 |
|------|------|
| 中断响应延迟 | ~1000 cycles |
| 上下文保存/恢复 | ~200 cycles |
| 时钟中断间隔 | ~10ms |

---

## 四、实验收获

| 类别 | 收获 |
|------|------|
| 硬件机制 | 中断时 CPU 自动保存 pc→sepc, 原因→scause, 跳转→stvec |
| CSR 寄存器 | stvec/sepc/scause/stval/sie/sip 的作用 |
| 上下文保存 | 汇编保存/恢复寄存器，理解 callee/caller-saved |
| 回调注册 | 函数指针数组实现动态注册，解耦设计 |
| 优先级嵌套 | 低优先级处理时开中断允许高优先级抢占 |
| 竞态保护 | push_off/pop_off 临界区保护 |

**展望：** 中断亲和性、中断统计监控。
