# 扩展实验 8-5：写时复制 (Copy-on-Write) Fork

## 一、系统设计部分

### 1.1 架构设计说明

本实验实现 COW 机制，使 `fork()` 从 O(内存大小) 优化为 O(页表大小)。

**传统 fork 的问题：**
- 1GB 内存的进程 fork 需复制 1GB
- 复制耗时（内存带宽限制）
- 子进程常立即 exec，复制的内存浪费

**COW 核心思想：延迟复制**
- fork 时共享物理内存，标记为只读
- 写入时才复制该页面
- 基于观察：大部分页面只读或不被访问

**实现流程：**

| 阶段 | 操作 |
|------|------|
| fork | 共享物理页，标记只读+COW，引用计数+1 |
| 写入 | 页错误 → 检查 COW → refcnt>1 则复制，==1 则直接改可写 |

**本实验实现：**
- 引用计数管理（page_incref/decref）
- PTE 保留位标记 COW 页面
- 写时缺页处理（cow_fault）
- 内存效率验证

#### 系统架构图

```
┌─────────────────────────────────────────────────────┐
│                    应用层                            │
│              fork() / 进程内存访问                   │
├─────────────────────────────────────────────────────┤
│                  COW 管理层                          │
│     引用计数 / COW 标记 / 缺页处理                  │
├─────────────────────────────────────────────────────┤
│                  页表管理层                          │
│         PTE 标志位 / 页面映射                       │
├─────────────────────────────────────────────────────┤
│                 物理内存管理层                       │
│            页面分配 / 引用计数表                    │
└─────────────────────────────────────────────────────┘
```

#### 文件组织

```
exp8-5/
├── include/
│   ├── cow.h            # COW 系统接口声明
│   └── riscv.h          # 添加 PTE_C 标志位
├── mm/
│   └── cow.c            # COW 实现
└── main.c               # 测试主函数
```

### 1.2 关键数据结构

#### 引用计数表

```c
/* 每个物理页一个引用计数 */
static int page_refcount[NPHYSPAGES];

#define PA2IDX(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)
```

#### COW 页表项标志

```c
#define PTE_C  (1L << 8)  /* COW 标志位 (使用 RSW 保留位) */

/* COW 页面: 有效、可读、不可写、COW 标记 */
#define PTE_COW  (PTE_V | PTE_R | PTE_C)
```

### 1.3 COW 工作原理

#### 传统 fork vs COW fork

```
传统 fork:
  父进程: [Page A] [Page B] [Page C]
                ↓ fork() 复制所有页面
  子进程: [Page A'] [Page B'] [Page C']
  内存使用: 6 页

COW fork:
  父进程: [Page A] [Page B] [Page C]
                ↓ fork() 共享页面，标记为只读
  子进程: ───────↗ ───────↗ ───────↗
  内存使用: 3 页 (共享)

  子进程写入 Page A:
  父进程: [Page A] [Page B] [Page C]
  子进程: [Page A'] ──────↗ ───────↗
  内存使用: 4 页 (按需复制)
```

#### COW 缺页处理流程

```
1. 进程写入 COW 页面
2. 触发 Store Page Fault (scause = 15)
3. 检查 PTE_C 标志
4. 如果 refcount == 1:
   - 直接升级为可写 (无需复制)
5. 如果 refcount > 1:
   - 分配新页面
   - 复制数据
   - 更新页表指向新页面
   - 原页面 refcount--
6. 返回用户态，重试写入
```

### 1.4 与 xv6 对比分析

| 方面 | 本实验 (exp8-5) | xv6-riscv |
|------|-----------------|-----------|
| **COW 支持** | 完整实现 | 不支持 |
| **引用计数** | 全局数组 | 无 |
| **fork 效率** | O(页表大小) | O(内存大小) |
| **内存节省** | 显著 | 无 |

---

## 二、实验过程部分

### 2.1 实现步骤记录

#### 步骤 1：引用计数管理

```c
void page_incref(uint64 pa) {
    int idx = PA2IDX(pa);
    if (idx >= 0 && idx < NPHYSPAGES)
        page_refcount[idx]++;
}

void page_decref(uint64 pa) {
    int idx = PA2IDX(pa);
    if (idx >= 0 && idx < NPHYSPAGES && page_refcount[idx] > 0)
        page_refcount[idx]--;
}

int page_getref(uint64 pa) {
    int idx = PA2IDX(pa);
    if (idx >= 0 && idx < NPHYSPAGES)
        return page_refcount[idx];
    return 0;
}
```

**关键点：**
- **PA2IDX 宏**：将物理地址转换为数组索引，通常是 `(pa - KERNBASE) / PGSIZE`
- **边界检查**：防止越界访问 page_refcount 数组
- **原子性**：多核环境需要使用原子操作或加锁保护
- **初始值**：alloc_page 返回的页面引用计数应初始化为 1
- **释放时机**：引用计数降为 0 时应该释放物理页

#### 步骤 2：COW fork 实现

```c
int uvmcopy_cow(pagetable_t old, pagetable_t new, uint64 sz) {
    for (uint64 va = 0; va < sz; va += PGSIZE) {
        pte_t *pte = walk(old, va, 0);
        if (pte == 0 || (*pte & PTE_V) == 0)
            continue;
        
        uint64 pa = PTE_PA(*pte);
        uint64 flags = PTE_FLAGS(*pte);
        
        /* 标记为 COW (清除写权限，设置 COW 标志) */
        if (flags & PTE_W) {
            flags = (flags & ~PTE_W) | PTE_C;
            *pte = PA2PTE(pa) | flags;
        }
        
        /* 子进程共享同一物理页 */
        map_page(new, va, pa, flags);
        
        /* 增加引用计数 */
        page_incref(pa);
    }
    return 0;
}
```

**关键点：**
- **遍历页表**：只处理有效的页表项（PTE_V 置位）
- **清除写权限**：将 PTE_W 清除，写操作会触发页错误
- **设置 COW 标志**：PTE_C 使用 RSW（Reserved for Software）位
- **父子共享**：子进程映射到相同的物理页，不复制数据
- **引用计数增加**：物理页现在被两个进程引用
- **只读页不变**：本来就是只读的页面无需设置 COW 标志

#### 步骤 3：COW 缺页处理

```c
int cow_fault(uint64 va) {
    struct proc *p = myproc();
    pte_t *pte = walk(p->pagetable, va, 0);
    
    if (pte == 0 || (*pte & PTE_V) == 0)
        return -1;
    
    /* 检查是否是 COW 页面 */
    if ((*pte & PTE_C) == 0)
        return -1;
    
    uint64 pa = PTE_PA(*pte);
    int ref = page_getref(pa);
    
    if (ref == 1) {
        /* 唯一引用者，直接升级为可写 */
        *pte = (*pte & ~PTE_C) | PTE_W;
    } else {
        /* 多个引用者，需要复制 */
        void *new_page = alloc_page();
        if (new_page == 0)
            return -1;
        
        memmove(new_page, (void*)pa, PGSIZE);
        
        /* 更新页表 */
        uint64 flags = (PTE_FLAGS(*pte) & ~PTE_C) | PTE_W;
        *pte = PA2PTE((uint64)new_page) | flags;
        
        /* 更新引用计数 */
        page_incref((uint64)new_page);
        page_decref(pa);
    }
    
    sfence_vma();
    return 0;
}
```

**关键点：**
- **COW 标志检查**：只处理带有 PTE_C 标志的页面，其他页错误返回错误
- **引用计数优化**：ref == 1 时无需复制，直接恢复写权限（重要优化）
- **复制时机**：只有在真正写入且有多个引用者时才复制（延迟复制）
- **页表更新**：清除 COW 标志，设置写权限，指向新页面
- **引用计数维护**：新页面 +1，旧页面 -1
- **TLB 刷新**：sfence.vma 确保后续访问使用新的页表项
- **内存分配失败**：返回 -1，调用者应该杀死进程

### 2.2 问题与解决方案

#### 问题 1：TLB 不一致

**现象：** 修改页表后仍访问旧映射

**原因：** TLB 缓存了旧的页表项

**解决方案：** 修改页表后执行 `sfence.vma`

#### 问题 2：引用计数泄漏

**现象：** 内存逐渐耗尽

**原因：** 进程退出时未正确减少引用计数

**解决方案：** 在 `freevm` 中正确处理 COW 页面

---

## 三、测试验证部分

### 3.1 功能测试结果

```bash
$ make run exp8-5
========================================
    Exp8-5: Copy-on-Write Fork
========================================

Test 1: Reference Count Basic
  Initial refcount: 0
  After incref: 1
  After second incref: 2
  After decref: 1
  After second decref: 0
Test 1 PASS!

Test 2: Multiple Pages Reference Count
  Page 0 refcount: 1
  Page 1 refcount: 1
  Page 2 refcount: 1
  Page 3 refcount: 1
  Page 4 refcount: 1
  After sharing (all +1):
    Page 0 refcount: 2
    Page 1 refcount: 2
    Page 2 refcount: 2
    Page 3 refcount: 2
    Page 4 refcount: 2
Test 2 PASS!

Test 3: COW Fork Simulation
  fork(): parent & child share page, refcount=2
  Child writes -> COW triggered
  Result: parent=42, child=100 (independent!)
Test 3 PASS!

Test 4: COW Write Performance (Page Fault Overhead)
  Allocating 8 pages for parent process...
  fork() - child shares all pages (COW)...
  Child writes to all 8 pages (triggering COW)...
  Results:
    Pages copied (COW faults): 8
    +----------------------------------+
    | COW write time:       1931 cycles |
    | Standard write time:    50 cycles |
    | Overhead:             1881 cycles |
    +----------------------------------+
    COW is slower due to page copy on write
  Verifying data independence:
    Parent data intact: YES
    Child data modified: YES
  Trade-off: Slower writes, but faster fork() & less memory!
Test 4 PASS!

Test 5: Memory Efficiency Demo
  Free pages before: 32440
  Simulating 10 forks (read-only)...
  Refcount after 10 forks: 11
  Free pages after: 32439
  Pages saved by COW: 10 (vs 10 without COW)
Test 5 PASS!

========================================
    All tests PASSED!
========================================
```

**测试解读：**
- **Test 1 引用计数基本操作**：incref/decref 正确增减引用计数（0→1→2→1→0），验证了引用计数管理的正确性
- **Test 2 多页引用计数**：5 个页面各自独立计数，共享后全部变为 2，说明批量操作正确
- **Test 3 COW Fork 模拟**：fork 后 refcount=2（父子共享），子进程写入触发 COW，最终 parent=42、child=100 互不影响，验证了数据独立性
- **Test 4 COW 写性能**：
  - COW 写入 1931 cycles vs 标准写入 50 cycles，开销 1881 cycles
  - 开销来自：页错误处理 + 页面复制 + 页表更新 + TLB 刷新
  - 父子数据独立验证通过，说明复制正确
- **Test 5 内存效率**：10 次 fork 只消耗 1 页（32440→32439），引用计数为 11（1 原始 + 10 共享），节省 90% 内存

### 3.2 性能数据

| 场景 | 传统 fork | COW fork | 节省 |
|------|-----------|----------|------|
| fork 10 个进程 (只读) | 10 页 | 1 页 | 90% |
| fork 后全部写入 | 10 页 | 10 页 | 0% (但延迟分配) |
| fork 后部分写入 | 10 页 | 1 + 写入页数 | 取决于写入比例 |

| 操作 | 时间 |
|------|------|
| COW 写入 (含复制) | ~2000 cycles |
| 标准写入 | ~50 cycles |
| COW 开销 | ~1950 cycles/页 |

---

## 四、实验收获

| 类别 | 收获 |
|------|------|
| 延迟复制 | "不到万不得已不复制"，惰性求值策略 |
| 引用计数 | 管理共享资源，注意循环引用问题 |
| PTE 标志位 | 利用 RSW 保留位存储 COW 标志 |
| 页错误类型 | 存储页错误（scause=15）触发 COW |
| 处理流程 | 检查 COW→检查 refcnt→分配→复制→更新页表→sfence.vma |
| 性能权衡 | fork 快但写入有开销，fork+exec 模式最优 |
| 内存节省 | 10 次只读 fork 仅需 1 页物理内存 |

**展望：** COW 统计、大页 COW、KSM 页面合并、NUMA 优化。
