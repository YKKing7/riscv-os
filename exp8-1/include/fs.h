/*
 * fs.h - 文件系统接口定义
 * 
 * 功能：
 *   - 磁盘布局与超级块定义
 *   - inode 结构定义
 *   - 目录项结构定义
 *   - 文件系统常量
 */

#ifndef FS_H
#define FS_H

#include "types.h"

/* ============== 文件系统常量 ============== */

#define BSIZE       1024    /* 块大小 (字节) */
#define FSMAGIC     0x10203040

#define ROOTINO     1       /* 根目录 inode 号 */
#define ROOTDEV     0       /* 根设备号 */

#define NDIRECT     12      /* 直接块数量 */
#define NINDIRECT   (BSIZE / sizeof(uint32))  /* 间接块数量 */
#define MAXFILE     (NDIRECT + NINDIRECT)     /* 最大文件块数 */

#define DIRSIZ      14      /* 目录名最大长度 */

/* 文件类型 */
#define T_DIR       1       /* 目录 */
#define T_FILE      2       /* 普通文件 */
#define T_DEVICE    3       /* 设备文件 */

/* ============== 磁盘布局 ============== */

/*
 * 磁盘布局:
 * [ boot | super | log ... | inode blocks ... | bitmap | data blocks ... ]
 *    0      1      2         logsize+2          ...       ...
 */

/* 超级块结构 */
struct superblock {
    uint32 magic;       /* 魔数: FSMAGIC */
    uint32 size;        /* 文件系统总块数 */
    uint32 nblocks;     /* 数据块数量 */
    uint32 ninodes;     /* inode 数量 */
    uint32 nlog;        /* 日志块数量 */
    uint32 logstart;    /* 日志区起始块号 */
    uint32 inodestart;  /* inode 区起始块号 */
    uint32 bmapstart;   /* 位图区起始块号 */
};

/* ============== inode 结构 ============== */

/* 磁盘 inode 结构 */
struct dinode {
    int16  type;        /* 文件类型 (0=空闲) */
    int16  major;       /* 主设备号 (T_DEVICE) */
    int16  minor;       /* 次设备号 (T_DEVICE) */
    int16  nlink;       /* 硬链接计数 */
    uint32 size;        /* 文件大小 (字节) */
    uint32 addrs[NDIRECT + 1];  /* 数据块地址 (最后一个是间接块) */
};

/* 每块包含的 inode 数量 */
#define IPB         (BSIZE / sizeof(struct dinode))

/* inode i 所在的块号 */
#define IBLOCK(i, sb)   ((i) / IPB + (sb).inodestart)

/* 每块包含的位图位数 */
#define BPB         (BSIZE * 8)

/* 块 b 对应的位图块号 */
#define BBLOCK(b, sb)   ((b) / BPB + (sb).bmapstart)

/* ============== 目录结构 ============== */

/* 目录项结构 */
struct dirent {
    uint16 inum;            /* inode 号 (0=空闲) */
    char   name[DIRSIZ];    /* 文件名 */
};

/* ============== 内存 inode 结构 ============== */

struct inode {
    uint32 dev;         /* 设备号 */
    uint32 inum;        /* inode 号 */
    int    ref;         /* 引用计数 */
    int    valid;       /* 是否已从磁盘读取 */
    
    /* 以下字段从磁盘 inode 复制 */
    int16  type;
    int16  major;
    int16  minor;
    int16  nlink;
    uint32 size;
    uint32 addrs[NDIRECT + 1];
};

/* ============== 文件系统接口 ============== */

/* 初始化 */
void fsinit(int dev);

/* 块缓存 */
struct buf* bread(uint32 dev, uint32 blockno);
void        bwrite(struct buf *b);
void        brelse(struct buf *b);

/* inode 操作 */
struct inode* ialloc(uint32 dev, int16 type);
struct inode* iget(uint32 dev, uint32 inum);
void          ilock(struct inode *ip);
void          iunlock(struct inode *ip);
void          iput(struct inode *ip);
void          iunlockput(struct inode *ip);
void          iupdate(struct inode *ip);
void          itrunc(struct inode *ip);

/* 文件读写 */
int readi(struct inode *ip, char *dst, uint32 off, uint32 n);
int writei(struct inode *ip, char *src, uint32 off, uint32 n);

/* 目录操作 */
struct inode* dirlookup(struct inode *dp, char *name, uint32 *poff);
int           dirlink(struct inode *dp, char *name, uint32 inum);
int           dirunlink(struct inode *dp, char *name);

/* 路径解析 */
struct inode* namei(char *path);

/* 日志系统 */
void initlog(int dev, struct superblock *sb);
void begin_op(void);
void end_op(void);
void log_write(struct buf *b);

#endif /* FS_H */
