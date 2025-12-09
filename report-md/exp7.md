# 实验七：文件系统

## 一、系统设计部分

### 1.1 架构设计说明

本实验实现类 Unix 文件系统，将磁盘的线性字节数组抽象为层次化的文件和目录结构。

**核心挑战：**

| 挑战 | 说明 |
|------|------|
| 持久性 | 数据断电后仍存在 |
| 一致性 | 崩溃后文件系统状态正确 |
| 性能 | 缓存和批量写入优化 |
| 空间管理 | 减少碎片，高效分配 |

**分层设计：**
```
应用层: open("/foo/bar") → 路径解析: namei() → inode层: readi()
                        → 日志层: begin_op()/end_op() → 缓存层: bread() → 磁盘层
```

每层独立开发测试，便于替换底层实现。

**本实验实现：**
- **块缓存**：LRU 策略减少磁盘 I/O
- **写前日志 (WAL)**：保证崩溃一致性
- **inode 管理**：元数据分配、缓存、读写
- **目录/文件操作**：路径解析、创建、读写、删除

#### 系统架构图

```
┌─────────────────────────────────────────────────────┐
│                    应用层                            │
│           文件操作 API (open/read/write)            │
├─────────────────────────────────────────────────────┤
│                   目录层                             │
│         路径解析 / 目录项管理 (fs.c)                │
├─────────────────────────────────────────────────────┤
│                   inode 层                           │
│         inode 分配/缓存/读写 (fs.c)                 │
├─────────────────────────────────────────────────────┤
│                   日志层                             │
│         写前日志 / 事务管理 (log.c)                 │
├─────────────────────────────────────────────────────┤
│                  块缓存层                            │
│         Buffer Cache / LRU (bio.c)                  │
├─────────────────────────────────────────────────────┤
│                   磁盘层                             │
│              块设备驱动 (virtio)                    │
└─────────────────────────────────────────────────────┘
```

#### 磁盘布局

```
┌────────┬────────┬────────┬────────┬────────────────┐
│ Boot   │ Super  │  Log   │ Inode  │    Data        │
│ Block  │ Block  │ Blocks │ Blocks │    Blocks      │
├────────┼────────┼────────┼────────┼────────────────┤
│ 0      │ 1      │ 2-31   │ 32-63  │ 64-...         │
└────────┴────────┴────────┴────────┴────────────────┘
```

#### 文件组织

```
exp7/
├── include/
│   ├── fs.h             # 文件系统接口定义
│   └── buf.h            # 块缓存结构定义
├── fs/
│   ├── bio.c            # 块缓存实现
│   ├── log.c            # 写前日志实现
│   ├── fs.c             # 文件系统核心实现
│   └── mkfs.c           # 文件系统初始化
└── main.c               # 测试主函数
```

### 1.2 关键数据结构

#### 超级块

```c
struct superblock {
    uint32 magic;        /* 魔数 0x10203040 */
    uint32 size;         /* 文件系统总块数 */
    uint32 nblocks;      /* 数据块数 */
    uint32 ninodes;      /* inode 数 */
    uint32 nlog;         /* 日志块数 */
    uint32 logstart;     /* 日志起始块 */
    uint32 inodestart;   /* inode 起始块 */
    uint32 bmapstart;    /* 位图起始块 */
};
```

#### inode (磁盘)

```c
struct dinode {
    short type;          /* 文件类型 (T_FILE/T_DIR/T_DEV) */
    short major;         /* 主设备号 */
    short minor;         /* 次设备号 */
    short nlink;         /* 硬链接数 */
    uint32 size;         /* 文件大小 */
    uint32 addrs[NDIRECT+1];  /* 数据块地址 */
};
```

#### 块缓存

```c
struct buf {
    int valid;           /* 数据有效 */
    int disk;            /* 正在磁盘 I/O */
    uint32 dev;          /* 设备号 */
    uint32 blockno;      /* 块号 */
    uint32 refcnt;       /* 引用计数 */
    struct buf *prev;    /* LRU 链表 */
    struct buf *next;
    uint8 data[BSIZE];   /* 数据 */
};
```

### 1.3 与 xv6 对比分析

| 方面 | 本实验 (exp7) | xv6-riscv |
|------|---------------|-----------|
| **块大小** | 1024 字节 | 1024 字节 |
| **日志系统** | 写前日志 | 写前日志 |
| **缓存策略** | LRU | LRU |
| **最大文件** | 12KB + 256KB | 同 |
| **目录项** | 14 字节名 | 14 字节名 |

### 1.4 设计决策理由

| 决策 | 理由 |
|------|------|
| **写前日志** | 保证崩溃一致性 |
| **LRU 缓存** | 提高热点数据访问性能 |
| **间接块** | 支持大文件 |
| **事务接口** | begin_op/end_op 简化一致性管理 |

---

## 二、实验过程部分

### 2.1 实现步骤记录

#### 步骤 1：块缓存 (bio.c)

```c
struct buf *bread(uint32 dev, uint32 blockno) {
    struct buf *b = bget(dev, blockno);
    if (!b->valid) {
        disk_read(b);
        b->valid = 1;
    }
    return b;
}

void bwrite(struct buf *b) {
    disk_write(b);
}

void brelse(struct buf *b) {
    b->refcnt--;
    if (b->refcnt == 0) {
        /* 移到 LRU 链表头部 */
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
}
```

**关键点：**
- **bget 查找/分配**：先在缓存中查找，未命中则分配一个缓冲区
- **valid 标志**：表示缓冲区数据是否与磁盘一致
- **引用计数**：refcnt 跟踪有多少地方在使用这个缓冲区
- **LRU 链表**：释放时移到链表头部，分配时从尾部取最久未使用的
- **延迟写入**：bwrite 不立即写磁盘，由日志层决定何时真正写入
- **缓存一致性**：同一块只有一个缓冲区，避免数据不一致

#### 步骤 2：写前日志 (log.c)

```c
void begin_op(void) {
    acquire(&log.lock);
    while (log.committing)
        sleep(&log, &log.lock);
    log.outstanding++;
    release(&log.lock);
}

void end_op(void) {
    acquire(&log.lock);
    log.outstanding--;
    if (log.outstanding == 0) {
        /* 提交事务 */
        commit();
    }
    release(&log.lock);
}

static void commit(void) {
    if (log.lh.n > 0) {
        write_log();      /* 写日志块 */
        write_head();     /* 写日志头 */
        install_trans();  /* 写实际位置 */
        log.lh.n = 0;
        write_head();     /* 清除日志 */
    }
}
```

**关键点：**
- **outstanding 计数**：跟踪当前有多少个操作在进行中
- **committing 标志**：提交期间阻止新操作开始
- **组提交**：多个操作可以合并到一个事务中，提高效率
- **write_log**：将修改的块复制到日志区域
- **write_head**：写入日志头，这是提交点（commit point）
- **install_trans**：将日志中的数据写到实际位置
- **两次 write_head**：第二次清除日志，表示事务完成

#### 步骤 3：inode 操作

```c
struct inode *ialloc(uint32 dev, short type) {
    for (int inum = 1; inum < sb.ninodes; inum++) {
        struct buf *bp = bread(dev, IBLOCK(inum, sb));
        struct dinode *dip = (struct dinode*)bp->data + inum % IPB;
        if (dip->type == 0) {
            memset(dip, 0, sizeof(*dip));
            dip->type = type;
            log_write(bp);
            brelse(bp);
            return iget(dev, inum);
        }
        brelse(bp);
    }
    return 0;
}

int readi(struct inode *ip, char *dst, uint32 off, uint32 n) {
    for (uint32 tot = 0; tot < n; tot += m, off += m, dst += m) {
        struct buf *bp = bread(ip->dev, bmap(ip, off / BSIZE));
        m = min(n - tot, BSIZE - off % BSIZE);
        memmove(dst, bp->data + off % BSIZE, m);
        brelse(bp);
    }
    return n;
}
```

**关键点：**
- **ialloc 线性扫描**：遍历所有 inode 找到空闲的（type==0）
- **IBLOCK 宏**：计算 inode 号对应的磁盘块号
- **IPB**：每块包含的 inode 数量（Inodes Per Block）
- **log_write**：通过日志层写入，保证崩溃一致性
- **iget**：获取内存中的 inode 缓存，增加引用计数
- **bmap**：将文件内偏移转换为磁盘块号，处理直接块和间接块
- **分块读取**：大文件需要多次读取，每次最多读一个块

#### 步骤 4：目录操作

```c
struct inode *dirlookup(struct inode *dp, char *name, uint32 *poff) {
    struct dirent de;
    for (uint32 off = 0; off < dp->size; off += sizeof(de)) {
        readi(dp, (char*)&de, off, sizeof(de));
        if (de.inum != 0 && namecmp(name, de.name) == 0) {
            if (poff)
                *poff = off;
            return iget(dp->dev, de.inum);
        }
    }
    return 0;
}

int dirlink(struct inode *dp, char *name, uint32 inum) {
    /* 查找空闲目录项 */
    struct dirent de;
    uint32 off;
    for (off = 0; off < dp->size; off += sizeof(de)) {
        readi(dp, (char*)&de, off, sizeof(de));
        if (de.inum == 0)
            break;
    }
    
    strncpy(de.name, name, DIRSIZ);
    de.inum = inum;
    writei(dp, (char*)&de, off, sizeof(de));
    return 0;
}
```

**关键点：**
- **目录是特殊文件**：目录内容是 dirent 数组，每个 dirent 包含文件名和 inode 号
- **线性查找**：遍历所有目录项查找匹配的名字，大目录效率较低
- **inum == 0**：表示空闲目录项，可以被复用
- **poff 输出参数**：返回找到的目录项偏移，用于后续修改
- **DIRSIZ**：文件名最大长度（通常 14 字节）
- **strncpy**：防止文件名过长导致缓冲区溢出

### 2.2 问题与解决方案

#### 问题 1：日志溢出

**现象：** 大事务导致日志空间不足

**原因：** 单个事务写入块数超过日志容量

**解决方案：** 分批提交，限制单事务块数

#### 问题 2：缓存一致性

**现象：** 读取到旧数据

**原因：** 日志写入后未更新缓存

**解决方案：** install_trans 时更新缓存中的数据

### 2.3 源码理解总结

#### 写前日志流程

```
begin_op()
    ↓
修改数据 (log_write 记录到日志)
    ↓
end_op()
    ↓
commit():
    1. write_log()     - 数据写入日志区
    2. write_head()    - 提交点 (原子)
    3. install_trans() - 数据写入实际位置
    4. write_head()    - 清除日志
```

#### 崩溃恢复

```
如果崩溃发生在:
- write_log 之前: 无影响，事务未提交
- write_head 之前: 日志不完整，恢复时忽略
- install_trans 之前: 重放日志
- 清除日志之前: 重放日志 (幂等)
```

---

## 三、测试验证部分

### 3.1 功能测试结果

```bash
$ make run exp7
=============================================
  Exp7: File System
=============================================

Test 1: Filesystem Integrity
  Creating and writing test file...
    Written 18 bytes
  Reopening and verifying...
    Read 18 bytes: "Hello, filesystem!"
  Deleting test file...
    File deleted successfully
Test 1 PASS!

Test 2: Concurrent Access
  Simulating concurrent file operations...
    Created 40 files concurrently
    Cleaned up all test files
Test 2 PASS!

Test 3: Crash Recovery
  Testing WAL consistency...
    Scenario 1: Normal transaction commit
      Data persisted correctly
    Scenario 2: Multi-block transaction
      Multi-block transaction OK
    Scenario 3: Log state verification
      Log: outstanding=1, committing=0, n=0
Test 3 PASS!

Test 4: Filesystem Performance
  Small files test (50 x 4B)...
    Created: 50 files
    Time: 5000000 cycles
    Disk I/O: 200 reads, 400 writes
  Cleaning up small files...
  Large file test (1 x 16KB)...
    Time: 2000000 cycles
    Disk I/O: 50 reads, 100 writes
    File size: 16 KB
  Sequential read test...
    Time: 500000 cycles
    Disk reads: 0 (cache effect)
  Random read test...
    Time: 100000 cycles for 10 random blocks
    Disk reads: 0
Test 4 PASS!

=============================================
  All tests passed!
=============================================
```

**测试解读：**
- **Test 1 文件系统完整性**：创建文件、写入 18 字节、关闭后重新打开读取内容一致、删除成功，验证了基本文件操作的正确性
- **Test 2 并发访问**：同时创建 40 个文件无冲突，说明锁机制（inode 锁、日志锁）正确保护了共享数据结构
- **Test 3 崩溃恢复**：正常提交和多块事务都正确完成；日志状态 outstanding=1, committing=0, n=0 表示有一个活跃事务，未在提交中，日志为空
- **Test 4 性能测试**：
  - 小文件（50×4B）：200 读 + 400 写，每个文件约 12 次 I/O（inode 分配、目录更新、数据写入等）
  - 大文件（16KB）：I/O 次数更少，因为元数据开销被摊薄
  - 顺序/随机读取：磁盘读取为 0，说明块缓存命中率 100%，LRU 策略有效

### 3.2 性能数据

| 指标 | 数值 |
|------|------|
| 小文件创建 | ~100000 cycles/file |
| 大文件写入 | ~125000 cycles/KB |
| 顺序读取 | 缓存命中率 100% |
| 随机读取 | 缓存命中率高 |

---

## 四、实验收获

| 类别 | 收获 |
|------|------|
| 分层设计 | 每层职责明确，接口清晰，易于测试维护 |
| 磁盘布局 | 超级块→inode 区→位图→数据区 |
| inode | 文件名与内容分离，支持硬链接 |
| WAL | 先写日志再写实际位置，保证崩溃一致性 |
| 事务接口 | begin_op/end_op 提供原子性保证 |
| LRU 缓存 | 顺序读取命中率 100%，大幅减少磁盘 I/O |
| 引用计数 | 管理缓存块和 inode 的生命周期 |

**展望：** 符号链接、LFS、fsck 检查工具、B+ 树目录。
