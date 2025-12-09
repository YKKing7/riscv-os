# 实验五：进程管理与调度

## 一、系统设计部分

### 1.1 架构设计说明

本实验实现进程管理子系统。进程是资源分配和调度的基本单位，使多个程序可以"同时"运行。

**进程 = 程序 + 执行状态 + 资源**

| 组成 | 内容 |
|------|------|
| 代码和数据 | 程序指令和静态数据 |
| 执行状态 | PC、寄存器、栈 |
| 资源 | 打开文件、内存、信号处理器 |

**调度的必要性：**
- **CPU 复用**：快速切换实现"并发"
- **响应性**：交互任务快速响应
- **公平性**：合理分配 CPU 时间
- **效率**：I/O 等待时执行其他进程

**本实验实现：**
- 进程生命周期管理：`create_process`/`exit`/`wait`
- 上下文切换：保存/恢复 CPU 状态
- 调度算法：Round-Robin、Priority、MLFQ
- 同步原语：`sleep`/`wakeup`

#### 系统架构图

```
┌─────────────────────────────────────────────────────┐
│                    应用层                            │
│              用户进程 / 内核线程                     │
├─────────────────────────────────────────────────────┤
│                  进程管理层                          │
│     创建/销毁/同步 (proc.c)                         │
├─────────────────────────────────────────────────────┤
│                   调度器层                           │
│     RR / Priority / MLFQ (proc.c)                   │
├─────────────────────────────────────────────────────┤
│                 上下文切换层                         │
│            寄存器保存/恢复 (swtch.S)                │
└─────────────────────────────────────────────────────┘
```

#### 文件组织

```
exp5/
├── include/
│   └── proc.h           # 进程管理接口声明
├── proc/
│   ├── proc.c           # 进程管理实现
│   └── swtch.S          # 上下文切换汇编
└── main.c               # 测试主函数
```

### 1.2 关键数据结构

#### 进程控制块 (PCB)

```c
struct proc {
    int pid;                    /* 进程 ID */
    enum procstate state;       /* 进程状态 */
    char name[16];              /* 进程名 */
    
    struct context context;     /* 上下文 (callee-saved 寄存器) */
    struct trapframe *trapframe;/* 陷阱帧 */
    pagetable_t pagetable;      /* 页表 */
    
    int priority;               /* 优先级 (0-10) */
    int time_slice;             /* 时间片 */
    struct proc *parent;        /* 父进程 */
    int exit_status;            /* 退出状态 */
};
```

#### 进程状态

```c
enum procstate {
    UNUSED,     /* 未使用 */
    EMBRYO,     /* 创建中 */
    RUNNABLE,   /* 可运行 */
    RUNNING,    /* 运行中 */
    SLEEPING,   /* 睡眠 */
    ZOMBIE      /* 僵尸 */
};
```

#### 上下文结构

```c
struct context {
    uint64 ra;   /* 返回地址 */
    uint64 sp;   /* 栈指针 */
    uint64 s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
};
```

### 1.3 调度算法对比

| 算法 | 特点 | 适用场景 |
|------|------|----------|
| **RR** | 时间片轮转，公平 | 通用场景 |
| **Priority** | 高优先级优先 | 实时任务 |
| **MLFQ** | 多级反馈队列 | 交互式系统 |

### 1.4 与 xv6 对比分析

| 方面 | 本实验 (exp5) | xv6-riscv |
|------|---------------|-----------|
| **调度算法** | RR/Priority/MLFQ | 仅 RR |
| **优先级** | 0-10 级 | 无 |
| **MLFQ** | 3 级队列 | 无 |
| **进程同步** | sleep/wakeup | sleep/wakeup |
| **统计信息** | 切换次数/等待时间 | 无 |

---

## 二、实验过程部分

### 2.1 实现步骤记录

#### 步骤 1：上下文切换 (swtch.S)

```asm
.globl swtch
swtch:
    # 保存当前上下文到 a0 指向的结构
    sd ra, 0(a0)
    sd sp, 8(a0)
    sd s0, 16(a0)
    # ... 保存 s1-s11
    
    # 从 a1 指向的结构恢复上下文
    ld ra, 0(a1)
    ld sp, 8(a1)
    ld s0, 16(a1)
    # ... 恢复 s1-s11
    
    ret
```

**关键点：**
- **只保存 callee-saved 寄存器**：ra, sp, s0-s11 共 14 个寄存器，caller-saved 由调用约定保证
- **a0/a1 参数**：a0 指向当前进程的 context，a1 指向目标进程的 context
- **ret 指令**：从新的 ra 返回，实现控制流切换
- **栈切换**：恢复 sp 后，后续代码使用新进程的栈
- **透明切换**：从调用者角度看，swtch 就像普通函数调用，返回后继续执行

#### 步骤 2：进程创建

```c
int create_process(void (*entry)(void), const char *name) {
    struct proc *p = alloc_proc();
    if (p == 0) return -1;
    
    /* 分配内核栈 */
    p->kstack = alloc_page();
    
    /* 设置上下文，使 swtch 返回到 forkret */
    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;
    
    /* 设置入口点 */
    p->trapframe->epc = (uint64)entry;
    
    p->state = RUNNABLE;
    return p->pid;
}
```

**关键点：**
- **alloc_proc**：从进程表中找到一个 UNUSED 的槽位，分配 PID
- **内核栈**：每个进程有独立的内核栈，用于处理系统调用和中断
- **ra = forkret**：新进程第一次被调度时，swtch 返回到 forkret 函数
- **sp = kstack + PGSIZE**：栈向下增长，所以 sp 初始指向栈顶
- **trapframe->epc**：用户态入口点，从内核返回用户态时跳转到这里
- **状态转换**：设置为 RUNNABLE 后，调度器就可以选择这个进程运行

#### 步骤 3：调度器

```c
void scheduler(void) {
    for (;;) {
        struct proc *p = pick_next_process();
        if (p) {
            p->state = RUNNING;
            swtch(&cpu->context, &p->context);
            /* 返回时进程已让出 CPU */
        }
    }
}
```

**关键点：**
- **无限循环**：调度器永远运行，不断选择下一个进程
- **pick_next_process**：根据调度策略选择下一个 RUNNABLE 进程
- **状态设置**：切换前将进程状态改为 RUNNING
- **双向切换**：swtch 保存调度器上下文，恢复进程上下文
- **返回时机**：进程调用 yield/sleep/exit 时会 swtch 回调度器
- **空闲处理**：如果没有可运行进程，可以执行 wfi 等待中断

#### 步骤 4：MLFQ 调度

```c
struct proc *pick_mlfq(void) {
    /* 优先从高优先级队列选择 */
    for (int q = 0; q < MLFQ_LEVELS; q++) {
        for (struct proc *p = proc; p < &proc[NPROC]; p++) {
            if (p->state == RUNNABLE && p->mlfq_level == q)
                return p;
        }
    }
    return 0;
}

void mlfq_demote(struct proc *p) {
    /* CPU 密集型任务降级 */
    if (p->mlfq_level < MLFQ_LEVELS - 1)
        p->mlfq_level++;
}
```

**关键点：**
- **多级队列**：MLFQ_LEVELS 个优先级队列，level 0 最高
- **优先级遍历**：从最高优先级队列开始查找，保证高优先级任务优先执行
- **自动降级**：用完时间片的进程被降级到更低优先级队列
- **I/O 友好**：I/O 密集型任务经常阻塞，不会用完时间片，保持高优先级
- **防饥饿**：可以定期将所有进程提升到最高优先级（priority boost）
- **时间片递增**：低优先级队列可以有更长的时间片，减少切换开销

### 2.2 问题与解决方案

#### 问题 1：进程切换后栈损坏

**现象：** 切换回进程后数据错误

**原因：** 未保存所有 callee-saved 寄存器

**解决方案：** 确保 s0-s11 全部保存/恢复

#### 问题 2：僵尸进程累积

**现象：** 进程表耗尽

**原因：** 父进程未调用 wait 回收子进程

**解决方案：** 实现 init 进程收养孤儿进程

### 2.3 源码理解总结

#### 进程生命周期

```
UNUSED → EMBRYO → RUNNABLE ⇄ RUNNING → ZOMBIE → UNUSED
                     ↓
                  SLEEPING
```

#### MLFQ 工作原理

```
Queue 0 (最高优先级): [IO密集型任务]
         ↓ 用完时间片降级
Queue 1 (中优先级):   [混合型任务]
         ↓ 用完时间片降级
Queue 2 (最低优先级): [CPU密集型任务]
```

---

## 三、测试验证部分

### 3.1 功能测试结果

```bash
$ make run exp5
=============================================
  Exp5: Process Management & Scheduling
=============================================

Test 1: Process Creation & Destruction
  Created process PID=3
  [Task 0] Hello from PID=3
  [Task 0] Goodbye!
  Created 62 processes (limit test)
  All test processes cleaned up
Test 1 PASS!

Test 2: Basic Scheduler
  [CPU Task 4] Started
  [CPU Task 5] Started
  [CPU Task 6] Started
  [CPU Task 4] Round 1 complete
  ...
  Scheduler test completed in 50000000 cycles
Test 2 PASS!

Test 3: Synchronization
  [Producer] Started
  [Consumer] Started
  [Producer] Produced: 10
  [Consumer] Consumed: 10
  [Producer] Produced: 20
  [Consumer] Consumed: 20
  [Producer] Produced: 30
  [Consumer] Consumed: 30
  Producer-Consumer synchronization verified
Test 3 PASS!

Test 4: Debug Process Table
  +-----+--------+----------+------+
  | PID | Name   | State    | Prio |
  +-----+--------+----------+------+
  |   1 | init   | SLEEPING |    5 |
  |   2 | test   | RUNNING  |    5 |
  |   7 | dbg0   | RUNNABLE |    5 |
  +-----+--------+----------+------+
Test 4 PASS!

Test 5: Priority Scheduler
  Workload: 2 HIGH + 2 LOW priority tasks

  +-----------+----------+-------+----------+
  | Algorithm | Switches | Total | 1st Done |
  +-----------+----------+-------+----------+
  | Priority  |       12 |   500 |      100 |
  | RR        |       20 |   800 |      400 |
  +-----------+----------+-------+----------+

  -> Priority: HIGH tasks finish first!
Test 5 PASS!

Test 6: MLFQ Scheduler
  Workload: 2 CPU-bound + 2 IO-bound tasks

  +-----------+----------+-------+----------+
  | Algorithm | Switches | Total | 1st IO   |
  +-----------+----------+-------+----------+
  | MLFQ      |       80 |  1000 |       50 |
  | RR        |       40 |  1000 |      500 |
  +-----------+----------+-------+----------+

  -> MLFQ: IO tasks get better response time!
Test 6 PASS!

=============================================
  All tests PASSED!
=============================================
```

**测试解读：**
- **Test 1 进程创建销毁**：成功创建 PID=3 的进程并运行，创建 62 个进程测试了进程表上限，清理后无泄漏
- **Test 2 基本调度**：3 个 CPU 任务交替执行多轮，说明时间片轮转和 swtch 上下文切换正确
- **Test 3 同步机制**：生产者-消费者按序执行（10→10→20→20→30→30），说明 sleep/wakeup 同步原语正确
- **Test 4 进程表调试**：显示了进程状态（init 睡眠、test 运行、dbg0 就绪），验证了状态管理正确
- **Test 5 优先级调度**：Priority 算法切换次数更少（12 vs 20），高优先级任务更快完成（100 vs 400），符合优先级调度特性
- **Test 6 MLFQ 调度**：IO 任务响应时间大幅改善（50 vs 500），因为 MLFQ 自动提升频繁阻塞的任务优先级

### 3.2 性能数据

| 指标 | RR | Priority | MLFQ |
|------|-----|----------|------|
| 上下文切换次数 | 多 | 少 | 中 |
| 高优先级响应 | 慢 | 快 | 快 |
| IO 任务响应 | 慢 | 取决于优先级 | 快 |
| 公平性 | 高 | 低 | 中 |

---

## 四、实验收获

| 类别 | 收获 |
|------|------|
| 进程抽象 | PCB 记录进程所有状态，是管理核心 |
| 上下文切换 | 只需保存 callee-saved 寄存器（s0-s11, ra, sp） |
| 状态机 | UNUSED→EMBRYO→RUNNABLE⇄RUNNING→ZOMBIE |
| RR 调度 | 简单公平，但不区分任务类型 |
| Priority | 重要任务快速响应，但可能饥饿 |
| MLFQ | 自动分类：CPU 密集降级，I/O 密集保持高优先级 |
| 同步原语 | sleep/wakeup 实现条件等待/唤醒 |
| 僵尸进程 | wait 回收子进程 PCB，init 收养孤儿 |

**展望：** 抢占式调度、CFS 算法、优先级继承。
