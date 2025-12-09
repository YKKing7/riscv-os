/*
 * buf.h - 块缓存结构定义
 * 
 * 功能：
 *   - 块缓存 (buffer cache) 数据结构
 *   - LRU 链表管理
 */

#ifndef BUF_H
#define BUF_H

#include "types.h"
#include "fs.h"

/* 缓存块数量 */
#define NBUF        30

/* 块缓存结构 */
struct buf {
    int     valid;      /* 数据是否有效 */
    int     disk;       /* 是否正在进行磁盘操作 */
    uint32  dev;        /* 设备号 */
    uint32  blockno;    /* 块号 */
    uint32  refcnt;     /* 引用计数 */
    struct buf *prev;   /* LRU 链表前驱 */
    struct buf *next;   /* LRU 链表后继 */
    uint8   data[BSIZE];/* 块数据 */
};

/* 块缓存接口 */
void        binit(void);
struct buf* bread(uint32 dev, uint32 blockno);
void        bwrite(struct buf *b);
void        brelse(struct buf *b);
void        bpin(struct buf *b);
void        bunpin(struct buf *b);

#endif /* BUF_H */
