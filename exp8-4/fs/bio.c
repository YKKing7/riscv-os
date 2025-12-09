/*
 * bio.c - 块缓存 (Buffer Cache)
 * 
 * 功能：
 *   - 磁盘块缓存管理
 *   - LRU 替换策略
 *   - 减少磁盘 I/O 次数
 * 
 * 接口：
 *   - bread: 读取磁盘块到缓存
 *   - bwrite: 将缓存块写回磁盘
 *   - brelse: 释放缓存块引用
 */

#include "../include/types.h"
#include "../include/buf.h"
#include "../include/printf.h"

/* ============== 模拟磁盘 ============== */

#define DISK_SIZE   (1024 * 1024)   /* 1MB 模拟磁盘 */
static uint8 disk_data[DISK_SIZE];

/* 磁盘读写统计 */
static uint32 disk_read_count = 0;
static uint32 disk_write_count = 0;

/* 模拟磁盘读 */
static void disk_read(uint32 blockno, uint8 *data) {
    uint32 offset = blockno * BSIZE;
    if (offset + BSIZE <= DISK_SIZE) {
        for (int i = 0; i < BSIZE; i++)
            data[i] = disk_data[offset + i];
        disk_read_count++;
    }
}

/* 模拟磁盘写 */
static void disk_write(uint32 blockno, uint8 *data) {
    uint32 offset = blockno * BSIZE;
    if (offset + BSIZE <= DISK_SIZE) {
        for (int i = 0; i < BSIZE; i++)
            disk_data[offset + i] = data[i];
        disk_write_count++;
    }
}

/* ============== 块缓存 ============== */

static struct {
    struct buf buf[NBUF];
    struct buf head;        /* LRU 链表头 (哨兵) */
} bcache;

/* 初始化块缓存 */
void binit(void) {
    struct buf *b;
    
    /* 初始化 LRU 链表为空循环链表 */
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    
    /* 将所有缓存块加入 LRU 链表 */
    for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
        b->refcnt = 0;
        b->valid = 0;
        b->dev = 0;
        b->blockno = 0;
        
        /* 插入到链表头部 */
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
}

/* 获取指定块的缓存 (内部函数) */
static struct buf* bget(uint32 dev, uint32 blockno) {
    struct buf *b;
    
    /* 检查块是否已在缓存中 */
    for (b = bcache.head.next; b != &bcache.head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            return b;
        }
    }
    
    /* 未命中，从 LRU 尾部找一个空闲块 */
    for (b = bcache.head.prev; b != &bcache.head; b = b->prev) {
        if (b->refcnt == 0) {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
            return b;
        }
    }
    
    printf("bget: no buffers available\n");
    return 0;
}

/* 读取磁盘块 */
struct buf* bread(uint32 dev, uint32 blockno) {
    struct buf *b = bget(dev, blockno);
    if (!b) return 0;
    
    if (!b->valid) {
        disk_read(blockno, b->data);
        b->valid = 1;
    }
    return b;
}

/* 写回磁盘块 */
void bwrite(struct buf *b) {
    if (!b) return;
    disk_write(b->blockno, b->data);
}

/* 释放缓存块 */
void brelse(struct buf *b) {
    if (!b) return;
    
    b->refcnt--;
    if (b->refcnt == 0) {
        /* 移动到 LRU 链表头部 (最近使用) */
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
}

/* 固定缓存块 (日志系统使用) */
void bpin(struct buf *b) {
    if (b) b->refcnt++;
}

/* 解除固定 */
void bunpin(struct buf *b) {
    if (b) b->refcnt--;
}

/* ============== 调试接口 ============== */

/* 获取磁盘 I/O 统计 */
void get_disk_stats(uint32 *reads, uint32 *writes) {
    if (reads) *reads = disk_read_count;
    if (writes) *writes = disk_write_count;
}

/* 重置磁盘 I/O 统计 */
void reset_disk_stats(void) {
    disk_read_count = 0;
    disk_write_count = 0;
}

/* 获取模拟磁盘指针 (用于初始化文件系统) */
uint8* get_disk_data(void) {
    return disk_data;
}
