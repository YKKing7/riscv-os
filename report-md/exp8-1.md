# 扩展实验 8-1：优先级调度系统

## 一、系统设计部分

### 1.1 架构设计说明

本实验实现细粒度优先级控制和 Aging 防饥饿机制，允许重要任务获得更多 CPU 时间。

**任务类型与优先级需求：**

| 任务类型 | 示例 | 优先级需求 |
|----------|------|------------|
| 实时任务 | 音视频播放 | 高，需及时响应 |
| 交互任务 | 文本编辑 | 中高，快速响应输入 |
| 批处理 | 编译、备份 | 低，后台执行即可 |

**Aging 机制解决饥饿问题：**
- 进程等待时 `wait_ticks++`
- 达到阈值时优先级提升
- 运行后恢复基础优先级
- 保证低优先级进程最终能执行

**本实验实现：**
- 11 级优先级（0-10）
- Aging：等待 10 tick 后优先级 +1
- 运行时动态调整优先级
- RR/Priority/MLFQ 调度器可切换

#### 系统架构图

```
┌─────────────────────────────────────────────────────┐
│                    应用层                            │
│           用户进程 (不同优先级)                      │
├─────────────────────────────────────────────────────┤
│                  优先级管理层                        │
│     setpriority / getpriority / aging               │
├─────────────────────────────────────────────────────┤
│                   调度器层                           │
│     Priority / RR / MLFQ (可切换)                   │
├─────────────────────────────────────────────────────┤
│                 进程管理层                           │
│            进程表 / 上下文切换                       │
└─────────────────────────────────────────────────────┘
```

#### 文件组织

```
exp8-1/
├── include/
│   └── proc.h           # 扩展优先级定义
├── proc/
│   └── proc.c           # 优先级调度 + Aging 实现
└── main.c               # 测试主函数
```

### 1.2 关键数据结构

#### 优先级定义

```c
#define PRIO_MIN        0   /* 最低优先级 */
#define PRIO_MAX        10  /* 最高优先级 */
#define PRIO_DEFAULT    5   /* 默认优先级 */

/* Aging 参数 */
#define AGING_THRESHOLD 10  /* 等待多少 ticks 后提升优先级 */
#define AGING_INCREMENT 1   /* 每次提升的优先级增量 */
```

#### 进程扩展字段

```c
struct proc {
    /* ... 原有字段 ... */
    int priority;           /* 当前优先级 (0-10) */
    int base_priority;      /* 基础优先级 */
    int wait_ticks;         /* 等待时间计数 */
};
```

### 1.3 Aging 机制原理

```
问题: 低优先级进程可能永远得不到 CPU (饥饿)

解决: Aging 机制
  - 每个调度周期，等待中的进程 wait_ticks++
  - 当 wait_ticks >= AGING_THRESHOLD 时:
    - priority += AGING_INCREMENT
    - wait_ticks = 0
  - 进程运行后，priority 恢复为 base_priority

效果:
  时间 0:  HIGH(9) [运行]  LOW(1) [等待]
  时间 10: HIGH(9) [运行]  LOW(2) [等待, 提升]
  时间 20: HIGH(9) [运行]  LOW(3) [等待, 提升]
  ...
  时间 80: HIGH(9) [等待]  LOW(9) [运行, 追平]
```

### 1.4 与 xv6 对比分析

| 方面 | 本实验 (exp8-1) | xv6-riscv |
|------|-----------------|-----------|
| **优先级范围** | 0-10 (11级) | 无 |
| **Aging** | 支持 | 不支持 |
| **动态调整** | setpriority | 不支持 |
| **调度算法** | 可切换 | 仅 RR |

---

## 二、实验过程部分

### 2.1 实现步骤记录

#### 步骤 1：优先级调度器

```c
static struct proc *pick_priority(void) {
    struct proc *best = 0;
    int best_prio = -1;
    
    /* 先执行 aging 更新 */
    for (struct proc *p = proc; p < &proc[NPROC]; p++) {
        if (p->state == RUNNABLE) {
            p->wait_ticks++;
            if (p->wait_ticks >= AGING_THRESHOLD && p->priority < PRIO_MAX) {
                p->priority += AGING_INCREMENT;
                p->wait_ticks = 0;
            }
        }
    }
    
    /* 选择最高优先级进程 */
    for (struct proc *p = proc; p < &proc[NPROC]; p++) {
        if (p->state == RUNNABLE && p->priority > best_prio) {
            best = p;
            best_prio = p->priority;
        }
    }
    
    return best;
}
```

**关键点：**
- **两阶段处理**：先更新所有进程的 aging，再选择最高优先级进程
- **wait_ticks 累加**：每次调度时，所有等待的进程 wait_ticks 加 1
- **阈值触发**：达到 AGING_THRESHOLD 时提升优先级，重置计数器
- **优先级上限**：不能超过 PRIO_MAX，防止无限提升
- **线性扫描**：O(n) 复杂度，可以用优先队列优化到 O(log n)
- **运行后重置**：进程运行后应将 priority 恢复为 base_priority

#### 步骤 2：优先级系统调用

```c
int sys_setpriority(int pid, int prio) {
    if (prio < PRIO_MIN || prio > PRIO_MAX)
        return -1;
    
    struct proc *p = find_proc(pid);
    if (p == 0)
        return -1;
    
    p->priority = prio;
    p->base_priority = prio;
    return 0;
}

int sys_getpriority(int pid) {
    struct proc *p = find_proc(pid);
    if (p == 0)
        return -1;
    return p->priority;
}
```

**关键点：**
- **参数验证**：优先级必须在有效范围内，否则返回错误
- **进程查找**：通过 PID 查找进程，不存在则返回错误
- **同时更新两个字段**：priority 是当前优先级，base_priority 是基础优先级
- **权限检查**：完整实现应检查调用者是否有权修改目标进程的优先级
- **返回当前优先级**：getpriority 返回的是 aging 后的当前优先级

#### 步骤 3：进程创建扩展

```c
int create_process_prio(void (*entry)(void), const char *name, int prio) {
    int pid = create_process(entry, name);
    if (pid > 0) {
        struct proc *p = find_proc(pid);
        int valid_prio = (prio >= PRIO_MIN && prio <= PRIO_MAX) ? prio : PRIO_DEFAULT;
        p->priority = valid_prio;
        p->base_priority = valid_prio;
        p->wait_ticks = 0;
    }
    return pid;
}
```

**关键点：**
- **封装原有函数**：在 create_process 基础上添加优先级设置
- **参数校验**：无效优先级使用默认值，而非返回错误
- **初始化 wait_ticks**：新进程的等待计数从 0 开始
- **原子性**：进程创建和优先级设置应该是原子的，避免竞态条件
- **继承考虑**：也可以设计为继承父进程的优先级

### 2.2 问题与解决方案

#### 问题 1：Aging 不生效

**现象：** 低优先级任务仍然饥饿

**原因：** 优先级差距太大，Aging 来不及追平

**解决方案：** 调整 AGING_THRESHOLD 或缩小优先级差距

#### 问题 2：优先级反转

**现象：** 高优先级任务等待低优先级任务

**原因：** 低优先级任务持有资源

**解决方案：** 可实现优先级继承 (本实验未实现)

---

## 三、测试验证部分

### 3.1 功能测试结果

```bash
$ make run exp8-1
=============================================
  Exp8-1: Priority Scheduling System
=============================================

Test 1: Priority Gap (HIGH vs LOW)
  Workload: 2 HIGH (prio=8) + 2 LOW (prio=2) tasks
  [HIGH-0] Started (prio=8, pid=5)
  [HIGH-0] Finished
  [HIGH-1] Started (prio=8, pid=6)
  [HIGH-1] Finished
  [LOW-2] Started (prio=2, pid=3)
  [LOW-2] Finished
  [LOW-3] Started (prio=2, pid=4)
  [LOW-3] Finished
  Results:
    HIGH tasks finished first: YES
Test 1 PASS!

Test 2: Same Priority (RR behavior)
  Workload: 4 tasks with same priority (5)
  Results:
    Context switches: 8
    All tasks got fair execution (RR-like)
Test 2 PASS!

Test 3: Aging Mechanism (Anti-Starvation)
  +--------------------------------------------------+
  | Scenario: HIGH prio task monopolizes CPU         |
  | LOW prio task waits -> aging boosts its priority |
  +--------------------------------------------------+
  AGING_THRESHOLD = 10 ticks (boost after waiting)
  Created HIGH prio task (pid=11, prio=5)
  Created LOW  prio task (pid=12, prio=3) - will be starved
  [HIGH] Running (pid=11, prio=5) - monopolizing CPU...
  [LOW]  Got CPU! (pid=12) prio: 3 -> 5 (BOOSTED!)
  [LOW]  Finished (pid=12)
  [HIGH] Finished (pid=11)
  +--------------------------------------------------+
  | RESULT: Low priority task completed: YES         |
  | Aging mechanism prevented starvation!            |
  +--------------------------------------------------+
Test 3 PASS!

Test 4: setpriority/getpriority System Calls
  getpriority(13) = 5
  setpriority(13, 8) = 0
  getpriority(13) = 8 (after set)
  setpriority(13, 15) = -1 (invalid)
  getpriority(9999) = -1 (not found)
Test 4 PASS!

Test 5: Scheduler Comparison
  --- Priority Scheduler ---
  Finish order: 17 15 16 14 (highest first)
  --- RR Scheduler ---
  Finish order: 18 19 20 21 (FIFO)
Test 5 PASS!

=============================================
  All tests PASSED!
=============================================
```

**测试解读：**
- **Test 1 优先级差距**：HIGH 任务（prio=8）全部先于 LOW 任务（prio=2）完成，验证了优先级调度的基本正确性
- **Test 2 同优先级**：4 个同优先级任务产生 8 次切换，说明同优先级时退化为轮转调度，保证公平性
- **Test 3 Aging 机制**：LOW 任务等待 10 个 tick 后优先级从 3 提升到 5，成功获得 CPU 执行，验证了防饥饿机制有效
- **Test 4 系统调用**：getpriority/setpriority 正确工作；无效优先级（15）和不存在的 PID（9999）都返回 -1
- **Test 5 调度器对比**：Priority 调度按优先级顺序完成（17→15→16→14，数字大优先级高）；RR 调度按 FIFO 顺序完成（18→19→20→21）

### 3.2 性能数据

| 调度算法 | 高优先级响应 | 公平性 | 适用场景 |
|----------|--------------|--------|----------|
| Priority | 最快 | 低 (需 Aging) | 实时任务 |
| RR | 中等 | 高 | 通用 |
| MLFQ | 快 | 中 | 交互式 |

---

## 四、实验收获

| 类别 | 收获 |
|------|------|
| 优先级调度 | 选择最高优先级就绪进程，简单高效 |
| 饥饿问题 | 纯优先级调度的缺陷，低优先级可能永不执行 |
| Aging 机制 | 动态提升等待进程优先级，保证最终执行 |
| 参数调优 | 阈值/增量需根据工作负载调整 |
| 优先级反转 | 高优先级等待低优先级持有的锁，需优先级继承 |
| Linux CFS | 使用虚拟运行时间而非固定优先级 |

**展望：** 优先级继承、实时调度、CPU 亲和性、cgroup。
