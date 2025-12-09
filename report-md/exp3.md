# 实验三：物理内存管理与虚拟内存

## 一、系统设计部分

### 1.1 架构设计说明

本实验实现物理内存分配器和 Sv39 虚拟内存系统，是操作系统最核心的子系统之一。

**内存管理的必要性：**

| 需求 | 说明 |
|------|------|
| 资源复用 | 有限的物理内存需在多进程间共享 |
| 地址隔离 | 每个进程独立地址空间，互不干扰 |
| 权限控制 | 代码段只读可执行，数据段可读写 |
| 内存保护 | 防止用户程序访问内核内存 |

**本实验实现：**
- **物理内存分配器**：空闲链表实现 O(1) 分配/释放
- **伙伴系统**：连续多页分配，减少外部碎片
- **Sv39 页表**：三级页表，512GB 虚拟地址空间
- **大页映射**：2MB 大页，减少 TLB 压力

#### 系统架构图

```
┌─────────────────────────────────────────────────────┐
│                    应用层                            │
│                 kernel_main()                       │
├─────────────────────────────────────────────────────┤
│                  虚拟内存管理层                      │
│        页表管理 / 地址映射 (vm.c)                   │
├─────────────────────────────────────────────────────┤
│                  物理内存管理层                      │
│     空闲链表 / 伙伴系统 (pmm.c)                     │
├─────────────────────────────────────────────────────┤
│                    硬件层                            │
│         RISC-V Sv39 MMU / 物理内存                  │
└─────────────────────────────────────────────────────┘
```

#### 文件组织

```
exp3/
├── include/
│   ├── memlayout.h      # 内存布局定义
│   ├── riscv.h          # RISC-V 特权级定义
│   ├── pmm.h            # 物理内存管理接口
│   └── vm.h             # 虚拟内存管理接口
├── mm/
│   ├── pmm.c            # 物理内存管理实现
│   └── vm.c             # 虚拟内存管理实现
└── main.c               # 测试主函数
```

### 1.2 关键数据结构

#### 物理页管理

```c
/* 空闲页链表节点 */
struct free_page {
    struct free_page *next;
};

/* 伙伴系统空闲链表 */
struct {
    struct free_page *freelist[MAX_ORDER];  /* 每个 order 一个链表 */
} buddy;
```

#### Sv39 页表结构

```
虚拟地址 (39位):
┌────────┬────────┬────────┬────────────┐
│ VPN[2] │ VPN[1] │ VPN[0] │   Offset   │
│  9位   │  9位   │  9位   │    12位    │
└────────┴────────┴────────┴────────────┘

页表项 (PTE):
┌──────────────────────────┬───────────┐
│         PPN (44位)        │  Flags    │
│                          │ RWXUGVDA  │
└──────────────────────────┴───────────┘
```

#### 页表项标志位

| 标志 | 位置 | 说明 |
|------|------|------|
| V | bit 0 | 有效位 |
| R | bit 1 | 可读 |
| W | bit 2 | 可写 |
| X | bit 3 | 可执行 |
| U | bit 4 | 用户态可访问 |
| G | bit 5 | 全局映射 |
| A | bit 6 | 已访问 |
| D | bit 7 | 已修改 |

### 1.3 与 xv6 对比分析

| 方面 | 本实验 (exp3) | xv6-riscv |
|------|---------------|-----------|
| **物理内存分配** | 空闲链表 + 伙伴系统 | 仅空闲链表 |
| **页表级数** | 3 级 (Sv39) | 3 级 (Sv39) |
| **大页支持** | 2MB 大页映射 | 无 |
| **内存清零** | 可选 (alloc_page_nozero) | 始终清零 |
| **批量分配** | 伙伴系统支持 | 不支持 |

### 1.4 设计决策理由

| 决策 | 理由 |
|------|------|
| **空闲链表** | 实现简单，单页分配 O(1) |
| **伙伴系统** | 支持连续多页分配，减少外部碎片 |
| **延迟清零** | 提高分配性能，按需清零 |
| **恒等映射** | 内核直接访问物理内存，简化实现 |

---

## 二、实验过程部分

### 2.1 实现步骤记录

#### 步骤 1：物理内存分配器

```c
void pmm_init(void) {
    /* 将内核结束后的内存加入空闲链表 */
    char *p = (char*)PGROUNDUP((uint64)end);
    for (; p + PGSIZE <= (char*)PHYSTOP; p += PGSIZE)
        free_page(p);
}

void *alloc_page(void) {
    struct free_page *p = freelist;
    if (p)
        freelist = p->next;
    if (p)
        memset(p, 0, PGSIZE);  /* 清零 */
    return (void*)p;
}

void free_page(void *pa) {
    struct free_page *p = (struct free_page*)pa;
    p->next = freelist;
    freelist = p;
}
```

**关键点：**
- **end 符号**：由链接脚本定义，指向内核代码/数据结束的位置
- **PGROUNDUP**：将地址向上对齐到页边界，确保不覆盖内核数据
- **空闲页存储链表指针**：巧妙地利用空闲页本身存储 next 指针，无需额外元数据
- **头插法**：`free_page` 将页面插入链表头部，`alloc_page` 从头部取出，实现 O(1) 操作
- **清零时机**：分配时清零而非释放时，避免不必要的清零操作

#### 步骤 2：页表遍历 (walk)

```c
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pagetable[VA_VPN(va, level)];
        if (*pte & PTE_V) {
            pagetable = (pagetable_t)PTE_PA(*pte);
        } else {
            if (!alloc)
                return 0;
            pagetable = (pagetable_t)alloc_page();
            *pte = PA2PTE((uint64)pagetable) | PTE_V;
        }
    }
    return &pagetable[VA_VPN(va, 0)];
}
```

**关键点：**
- **三级遍历**：Sv39 使用三级页表，level 2→1→0 依次遍历
- **VPN 提取**：`VA_VPN(va, level)` 提取虚拟地址中对应级别的 9 位索引
- **按需分配**：`alloc` 参数控制是否在页表不存在时自动分配
- **PTE_V 检查**：只有 Valid 位为 1 的 PTE 才指向有效的下一级页表
- **返回值**：返回最终的 L0 PTE 指针，调用者可以读取或修改它

#### 步骤 3：页面映射

```c
int map_page(pagetable_t pt, uint64 va, uint64 pa, int perm) {
    pte_t *pte = walk(pt, va, 1);
    if (pte == 0)
        return -1;
    if (*pte & PTE_V)
        return -1;  /* 已映射 */
    *pte = PA2PTE(pa) | perm | PTE_V;
    return 0;
}
```

**关键点：**
- **walk 的 alloc=1**：如果中间级页表不存在，自动分配
- **重复映射检查**：如果 PTE 已经有效，返回错误，防止覆盖已有映射
- **PA2PTE 宏**：将物理地址转换为 PTE 格式（右移 12 位，左移 10 位）
- **权限位组合**：`perm` 包含 R/W/X/U 等权限，与 PTE_V 一起设置
- **返回值**：成功返回 0，失败返回 -1，便于错误处理

#### 步骤 4：伙伴系统

```c
void *buddy_alloc_pages(int n) {
    int order = 0;
    while ((1 << order) < n) order++;
    
    /* 查找足够大的块 */
    for (int o = order; o < MAX_ORDER; o++) {
        if (buddy.freelist[o]) {
            /* 分裂大块 */
            while (o > order) {
                split_block(o);
                o--;
            }
            return pop_block(order);
        }
    }
    return 0;
}
```

**关键点：**
- **order 计算**：将请求的页数向上取整到 2 的幂次（如 3 页 → order 2 = 4 页）
- **向上查找**：如果目标 order 没有空闲块，向更大的 order 查找
- **分裂操作**：将大块一分为二，一半放回较小 order 的空闲链表，另一半继续分裂或返回
- **伙伴地址**：order 为 k 的块，其伙伴地址 = 自身地址 XOR (1 << (k + 12))
- **合并优化**：释放时检查伙伴是否空闲，若空闲则合并成更大的块

### 2.2 问题与解决方案

#### 问题 1：页表映射后访问异常

**现象：** 启用虚拟内存后立即崩溃

**原因：** 内核代码未映射到虚拟地址空间

**解决方案：** 使用恒等映射，VA = PA

#### 问题 2：伙伴系统合并失败

**现象：** 释放后内存碎片化

**原因：** 伙伴地址计算错误

**解决方案：** 使用 XOR 计算伙伴地址：`buddy = addr ^ (1 << order)`

### 2.3 源码理解总结

#### Sv39 地址转换流程

```
VA → VPN[2] → L2 PTE → VPN[1] → L1 PTE → VPN[0] → L0 PTE → PA + Offset
```

#### 伙伴系统原理

```
Order 3 (8页):  [████████]
                    ↓ 分裂
Order 2 (4页):  [████] [████]
                  ↓ 分裂
Order 1 (2页):  [██] [██] [████]
                 ↓ 分裂
Order 0 (1页):  [█] [█] [██] [████]
```

---

## 三、测试验证部分

### 3.1 功能测试结果

```bash
$ make run exp3
=============================================
  Exp3: Physical & Virtual Memory Management
=============================================

Test 1: Physical Memory Manager
Test 1 PASS!

Test 2: Page Table Management
Test 2 PASS!

Test 3: Virtual Memory Activation
Test 3 PASS!

Test 4: Buddy System
Test 4 PASS!

Test 5: Allocation Performance
  [Opt 1] 64-bit vs byte memset
    Byte-by-byte: 32768 cycles/page
    64-bit word:  4096 cycles/page
    >>> Speedup:  87%
  [Opt 2] No-zero allocation
    Alloc only:     50 cycles/page
    Alloc + memset: 4146 cycles/page
    >>> Speedup:    98%
  [Opt 3] Buddy vs single (8 pages)
    Single x8:  800 cycles
    Buddy(8):   100 cycles
    >>> Speedup: 87%
Test 5 PASS!

Test 6: Huge Page Performance
  4KB: 2048 entries, 500000 cycles
  2MB: 4 entries, 1000 cycles
  >>> Speedup: 99%
Test 6 PASS!

=============================================
  All tests passed!
=============================================
```

**测试解读：**
- **Test 1 物理内存管理**：验证了 alloc_page/free_page 的基本正确性，包括分配、释放、重复分配等场景
- **Test 2 页表管理**：验证了三级页表的创建和遍历（walk 函数），确保 PTE 能正确定位
- **Test 3 虚拟内存激活**：验证了 satp 寄存器设置和 sfence.vma 刷新后，虚拟地址能正确翻译
- **Test 4 伙伴系统**：验证了块的分裂与合并逻辑，确保连续页分配和释放后能正确合并
- **Test 5 性能优化**：64 位清零比逐字节快 87%（利用 64 位总线宽度）；跳过清零快 98%（适用于立即覆写的场景）；伙伴系统批量分配快 87%（减少链表操作）
- **Test 6 大页性能**：2MB 大页比 4KB 小页快 99%，因为页表项从 2048 个减少到 4 个，TLB 压力大幅降低

### 3.2 性能数据

| 指标 | 数值 |
|------|------|
| 单页分配时间 | ~50 cycles |
| 64位清零速度 | 比逐字节快 87% |
| 伙伴系统批量分配 | 比单页快 87% |
| 大页映射 | 比小页快 99% |

---

## 四、实验收获

| 类别 | 收获 |
|------|------|
| 物理内存 | 空闲链表 O(1) 分配，用空闲页本身存储链表指针 |
| 页表机制 | Sv39 三级页表结构，多级页表节省内存 |
| 地址转换 | VA→PA 完整流程，TLB 加速作用 |
| 伙伴系统 | 二进制分裂/合并，伙伴地址 XOR 计算 |
| 大页优化 | 2MB 大页减少 TLB 压力，提升性能 99% |
| 硬件交互 | satp 寄存器启用分页，sfence.vma 刷新 TLB |

**展望：** 多核同步、按需分页、内存统计监控。
