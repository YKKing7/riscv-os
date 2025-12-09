/*
 * mkfs.c - 文件系统初始化
 * 
 * 功能：
 *   - 在内存磁盘上创建初始文件系统
 *   - 初始化超级块、位图、根目录
 */

#include "../include/types.h"
#include "../include/fs.h"
#include "../include/buf.h"
#include "../include/printf.h"

/* 外部函数声明 */
extern uint8* get_disk_data(void);
extern void binit(void);

/* ============== 文件系统参数 ============== */

#define FSSIZE      1000    /* 文件系统总块数 */
#define NINODES     200     /* inode 数量 */
#define NLOG        30      /* 日志块数 */

/* ============== 辅助函数 ============== */

static void* memset_local(void *dst, int c, uint32 n) {
    char *d = dst;
    for (uint32 i = 0; i < n; i++) d[i] = c;
    return dst;
}

static void* memmove_local(void *dst, const void *src, uint32 n) {
    char *d = dst;
    const char *s = src;
    for (uint32 i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

/* 写入一个块到磁盘 */
static void wsect(uint8 *disk, uint32 sec, void *buf) {
    memmove_local(disk + sec * BSIZE, buf, BSIZE);
}

/* 读取一个块从磁盘 */
static void rsect(uint8 *disk, uint32 sec, void *buf) {
    memmove_local(buf, disk + sec * BSIZE, BSIZE);
}

/* 写入 inode */
static void winode(uint8 *disk, uint32 inum, struct dinode *ip, struct superblock *sb) {
    uint8 buf[BSIZE];
    uint32 bn = IBLOCK(inum, *sb);
    rsect(disk, bn, buf);
    memmove_local(buf + (inum % IPB) * sizeof(struct dinode), ip, sizeof(struct dinode));
    wsect(disk, bn, buf);
}

/* 分配一个空闲块 */
static uint32 freeblock;

static uint32 balloc_mkfs(uint8 *disk, struct superblock *sb) {
    uint32 b = freeblock++;
    
    /* 标记位图 */
    uint8 buf[BSIZE];
    rsect(disk, BBLOCK(b, *sb), buf);
    buf[(b % BPB) / 8] |= 1 << (b % 8);
    wsect(disk, BBLOCK(b, *sb), buf);
    
    return b;
}

/* 追加数据到 inode */
static void iappend(uint8 *disk, uint32 inum, void *data, int n, struct superblock *sb) {
    uint8 buf[BSIZE];
    struct dinode din;
    
    /* 读取 inode */
    rsect(disk, IBLOCK(inum, *sb), buf);
    memmove_local(&din, buf + (inum % IPB) * sizeof(struct dinode), sizeof(din));
    
    uint32 off = din.size;
    
    while (n > 0) {
        uint32 bn = off / BSIZE;
        uint32 addr;
        
        if (bn < NDIRECT) {
            if (din.addrs[bn] == 0)
                din.addrs[bn] = balloc_mkfs(disk, sb);
            addr = din.addrs[bn];
        } else {
            /* 间接块 */
            if (din.addrs[NDIRECT] == 0)
                din.addrs[NDIRECT] = balloc_mkfs(disk, sb);
            
            uint32 indirect[NINDIRECT];
            rsect(disk, din.addrs[NDIRECT], indirect);
            
            if (indirect[bn - NDIRECT] == 0) {
                indirect[bn - NDIRECT] = balloc_mkfs(disk, sb);
                wsect(disk, din.addrs[NDIRECT], indirect);
            }
            addr = indirect[bn - NDIRECT];
        }
        
        /* 写入数据 */
        rsect(disk, addr, buf);
        int m = BSIZE - (off % BSIZE);
        if (m > n) m = n;
        memmove_local(buf + (off % BSIZE), data, m);
        wsect(disk, addr, buf);
        
        off += m;
        data = (char*)data + m;
        n -= m;
    }
    
    din.size = off;
    winode(disk, inum, &din, sb);
}

/* ============== 创建文件系统 ============== */

void mkfs(void) {
    uint8 *disk = get_disk_data();
    uint8 buf[BSIZE];
    struct superblock sb;
    struct dinode din;
    struct dirent de;
    
    /* 计算布局 */
    uint32 nbitmap = FSSIZE / BPB + 1;
    uint32 ninodeblocks = NINODES / IPB + 1;
    uint32 nmeta = 2 + NLOG + ninodeblocks + nbitmap;  /* boot + super + log + inodes + bitmap */
    uint32 nblocks = FSSIZE - nmeta;
    
    /* 初始化超级块 */
    memset_local(&sb, 0, sizeof(sb));
    sb.magic = FSMAGIC;
    sb.size = FSSIZE;
    sb.nblocks = nblocks;
    sb.ninodes = NINODES;
    sb.nlog = NLOG;
    sb.logstart = 2;
    sb.inodestart = 2 + NLOG;
    sb.bmapstart = 2 + NLOG + ninodeblocks;
    
    /* 清空磁盘 */
    memset_local(disk, 0, FSSIZE * BSIZE);
    
    /* 写入超级块 */
    memset_local(buf, 0, BSIZE);
    memmove_local(buf, &sb, sizeof(sb));
    wsect(disk, 1, buf);
    
    /* 初始化空闲块指针 */
    freeblock = nmeta;
    
    /* 创建根目录 inode (inum = 1) */
    memset_local(&din, 0, sizeof(din));
    din.type = T_DIR;
    din.nlink = 1;
    din.size = 0;
    winode(disk, ROOTINO, &din, &sb);
    
    /* 添加 "." 和 ".." 目录项 */
    memset_local(&de, 0, sizeof(de));
    de.inum = ROOTINO;
    de.name[0] = '.';
    iappend(disk, ROOTINO, &de, sizeof(de), &sb);
    
    de.name[1] = '.';
    iappend(disk, ROOTINO, &de, sizeof(de), &sb);
    
    /* 标记已使用的块 (元数据区) */
    for (uint32 i = 0; i < nmeta; i++) {
        rsect(disk, BBLOCK(i, sb), buf);
        buf[(i % BPB) / 8] |= 1 << (i % 8);
        wsect(disk, BBLOCK(i, sb), buf);
    }
}
