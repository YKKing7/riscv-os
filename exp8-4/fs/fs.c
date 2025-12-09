/*
 * fs.c - 文件系统核心实现
 * 
 * 功能：
 *   - 块分配与释放 (balloc/bfree)
 *   - inode 管理 (ialloc/iget/iput/ilock/iunlock)
 *   - 文件读写 (readi/writei)
 *   - 目录操作 (dirlookup/dirlink)
 *   - 路径解析 (namei/nameiparent)
 * 
 * 层次结构：
 *   Blocks -> Log -> Files -> Directories -> Names
 */

#include "../include/types.h"
#include "../include/fs.h"
#include "../include/buf.h"
#include "../include/printf.h"

/* ============== 全局变量 ============== */

struct superblock sb;           /* 超级块 */

#define NINODE      50          /* 内存 inode 缓存数量 */

static struct {
    struct inode inode[NINODE];
} itable;

/* ============== 辅助函数 ============== */

static int min(int a, int b) { return a < b ? a : b; }

static void* memset(void *dst, int c, uint32 n) {
    char *d = dst;
    for (uint32 i = 0; i < n; i++) d[i] = c;
    return dst;
}

static void* memmove(void *dst, const void *src, uint32 n) {
    char *d = dst;
    const char *s = src;
    if (d < s) {
        for (uint32 i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (uint32 i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dst;
}

static int strncmp(const char *s1, const char *s2, uint32 n) {
    for (uint32 i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
        if (s1[i] == 0) return 0;
    }
    return 0;
}

static char* strncpy(char *dst, const char *src, uint32 n) {
    uint32 i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

/* ============== 超级块 ============== */

/* 读取超级块 */
static void readsb(int dev, struct superblock *sb) {
    struct buf *bp = bread(dev, 1);
    memmove(sb, bp->data, sizeof(*sb));
    brelse(bp);
}

/* 初始化文件系统 */
void fsinit(int dev) {
    readsb(dev, &sb);
    if (sb.magic != FSMAGIC) {
        return;
    }
    
    initlog(dev, &sb);
}

/* ============== 块分配 ============== */

/* 清零一个块 */
static void bzero(int dev, int bno) {
    struct buf *bp = bread(dev, bno);
    memset(bp->data, 0, BSIZE);
    log_write(bp);
    brelse(bp);
}

/* 分配一个数据块 */
static uint32 balloc(uint32 dev) {
    struct buf *bp;
    
    for (uint32 b = 0; b < sb.size; b += BPB) {
        bp = bread(dev, BBLOCK(b, sb));
        
        for (uint32 bi = 0; bi < BPB && b + bi < sb.size; bi++) {
            int m = 1 << (bi % 8);
            if ((bp->data[bi / 8] & m) == 0) {
                /* 找到空闲块 */
                bp->data[bi / 8] |= m;
                log_write(bp);
                brelse(bp);
                bzero(dev, b + bi);
                return b + bi;
            }
        }
        brelse(bp);
    }

    return 0;
}

/* 释放一个数据块 */
static void bfree(int dev, uint32 b) {
    struct buf *bp = bread(dev, BBLOCK(b, sb));
    int bi = b % BPB;
    int m = 1 << (bi % 8);
    
    bp->data[bi / 8] &= ~m;
    log_write(bp);
    brelse(bp);
}

/* ============== inode 管理 ============== */

/* 分配一个 inode */
struct inode* ialloc(uint32 dev, int16 type) {
    struct buf *bp;
    struct dinode *dip;
    
    for (uint32 inum = 1; inum < sb.ninodes; inum++) {
        bp = bread(dev, IBLOCK(inum, sb));
        dip = (struct dinode*)bp->data + inum % IPB;
        
        if (dip->type == 0) {
            /* 找到空闲 inode */
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

/* 获取 inode (增加引用计数) */
struct inode* iget(uint32 dev, uint32 inum) {
    struct inode *ip, *empty = 0;
    
    /* 检查是否已在缓存中 */
    for (ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++) {
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            return ip;
        }
        if (empty == 0 && ip->ref == 0)
            empty = ip;
    }
    
    /* 使用空闲槽位 */
    if (empty == 0) {
        return 0;
    }
    
    ip = empty;
    ip->dev = dev;
    ip->inum = inum;
    ip->ref = 1;
    ip->valid = 0;
    return ip;
}

/* 锁定 inode 并从磁盘读取 */
void ilock(struct inode *ip) {
    struct buf *bp;
    struct dinode *dip;
    
    if (ip == 0 || ip->ref < 1) {
        return;
    }
    
    if (ip->valid == 0) {
        bp = bread(ip->dev, IBLOCK(ip->inum, sb));
        dip = (struct dinode*)bp->data + ip->inum % IPB;
        ip->type = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size = dip->size;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
        brelse(bp);
        ip->valid = 1;
    }
}

/* 解锁 inode */
void iunlock(struct inode *ip) {
    (void)ip;  /* 简化实现：无锁 */
}

/* 释放 inode 引用 */
void iput(struct inode *ip) {
    if (ip == 0) return;
    
    if (ip->ref == 1 && ip->valid && ip->nlink == 0) {
        /* 最后一个引用且无硬链接：删除文件 */
        itrunc(ip);
        ip->type = 0;
        iupdate(ip);
        ip->valid = 0;
    }
    
    ip->ref--;
}

/* 解锁并释放 */
void iunlockput(struct inode *ip) {
    iunlock(ip);
    iput(ip);
}

/* 将 inode 写回磁盘 */
void iupdate(struct inode *ip) {
    struct buf *bp;
    struct dinode *dip;
    
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum % IPB;
    dip->type = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
    log_write(bp);
    brelse(bp);
}

/* ============== 数据块映射 ============== */

/* 获取文件第 bn 个块的磁盘块号，必要时分配 */
static uint32 bmap(struct inode *ip, uint32 bn) {
    uint32 addr;
    uint32 *a;
    struct buf *bp;
    
    /* 直接块 */
    if (bn < NDIRECT) {
        if ((addr = ip->addrs[bn]) == 0) {
            addr = balloc(ip->dev);
            if (addr == 0) return 0;
            ip->addrs[bn] = addr;
        }
        return addr;
    }
    bn -= NDIRECT;
    
    /* 间接块 */
    if (bn < NINDIRECT) {
        /* 分配间接块 */
        if ((addr = ip->addrs[NDIRECT]) == 0) {
            addr = balloc(ip->dev);
            if (addr == 0) return 0;
            ip->addrs[NDIRECT] = addr;
        }
        
        bp = bread(ip->dev, addr);
        a = (uint32*)bp->data;
        
        if ((addr = a[bn]) == 0) {
            addr = balloc(ip->dev);
            if (addr) {
                a[bn] = addr;
                log_write(bp);
            }
        }
        brelse(bp);
        return addr;
    }
    
    return 0;
}

/* 截断文件 (释放所有数据块) */
void itrunc(struct inode *ip) {
    struct buf *bp;
    uint32 *a;
    
    /* 释放直接块 */
    for (int i = 0; i < NDIRECT; i++) {
        if (ip->addrs[i]) {
            bfree(ip->dev, ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }
    
    /* 释放间接块 */
    if (ip->addrs[NDIRECT]) {
        bp = bread(ip->dev, ip->addrs[NDIRECT]);
        a = (uint32*)bp->data;
        for (int j = 0; j < NINDIRECT; j++) {
            if (a[j])
                bfree(ip->dev, a[j]);
        }
        brelse(bp);
        bfree(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }
    
    ip->size = 0;
    iupdate(ip);
}

/* ============== 文件读写 ============== */

/* 读取文件数据 */
int readi(struct inode *ip, char *dst, uint32 off, uint32 n) {
    uint32 tot, m;
    struct buf *bp;
    
    if (off > ip->size || off + n < off)
        return 0;
    if (off + n > ip->size)
        n = ip->size - off;
    
    for (tot = 0; tot < n; tot += m, off += m, dst += m) {
        uint32 addr = bmap(ip, off / BSIZE);
        if (addr == 0) break;
        
        bp = bread(ip->dev, addr);
        m = min(n - tot, BSIZE - off % BSIZE);
        memmove(dst, bp->data + (off % BSIZE), m);
        brelse(bp);
    }
    return tot;
}

/* 写入文件数据 */
int writei(struct inode *ip, char *src, uint32 off, uint32 n) {
    uint32 tot, m;
    struct buf *bp;
    
    if (off > ip->size || off + n < off)
        return -1;
    if (off + n > MAXFILE * BSIZE)
        return -1;
    
    for (tot = 0; tot < n; tot += m, off += m, src += m) {
        uint32 addr = bmap(ip, off / BSIZE);
        if (addr == 0) break;
        
        bp = bread(ip->dev, addr);
        m = min(n - tot, BSIZE - off % BSIZE);
        memmove(bp->data + (off % BSIZE), src, m);
        log_write(bp);
        brelse(bp);
    }
    
    if (off > ip->size)
        ip->size = off;
    
    iupdate(ip);
    return tot;
}

/* ============== 目录操作 ============== */

static int namecmp(const char *s, const char *t) {
    return strncmp(s, t, DIRSIZ);
}

/* 在目录中查找文件 */
struct inode* dirlookup(struct inode *dp, char *name, uint32 *poff) {
    struct dirent de;
    
    if (dp->type != T_DIR) {
        return 0;
    }
    
    for (uint32 off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de)) {
            return 0;
        }
        if (de.inum == 0)
            continue;
        if (namecmp(name, de.name) == 0) {
            if (poff) *poff = off;
            return iget(dp->dev, de.inum);
        }
    }
    return 0;
}

/* 在目录中添加条目 */
int dirlink(struct inode *dp, char *name, uint32 inum) {
    struct dirent de;
    struct inode *ip;
    
    /* 检查名字是否已存在 */
    if ((ip = dirlookup(dp, name, 0)) != 0) {
        iput(ip);
        return -1;
    }
    
    /* 查找空闲目录项 */
    uint32 off;
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de)) {
            return -1;
        }
        if (de.inum == 0)
            break;
    }
    
    strncpy(de.name, name, DIRSIZ);
    de.inum = inum;
    if (writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
        return -1;
    
    return 0;
}

/* 从目录中删除条目 */
int dirunlink(struct inode *dp, char *name) {
    struct dirent de;
    uint32 off;
    
    if (dp->type != T_DIR)
        return -1;
    
    /* 查找目录项 */
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
            return -1;
        if (de.inum != 0 && namecmp(name, de.name) == 0) {
            /* 清空目录项 */
            memset(&de, 0, sizeof(de));
            if (writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
                return -1;
            return 0;
        }
    }
    return -1;  /* 未找到 */
}

/* ============== 路径解析 ============== */

/* 跳过路径中的一个元素 */
static char* skipelem(char *path, char *name) {
    while (*path == '/') path++;
    if (*path == 0) return 0;
    
    char *s = path;
    while (*path != '/' && *path != 0) path++;
    
    int len = path - s;
    if (len >= DIRSIZ)
        memmove(name, s, DIRSIZ);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    
    while (*path == '/') path++;
    return path;
}

/* 路径解析核心函数 */
static struct inode* namex(char *path, int nameiparent, char *name) {
    struct inode *ip, *next;
    
    if (*path == '/')
        ip = iget(ROOTDEV, ROOTINO);
    else
        ip = iget(ROOTDEV, ROOTINO);  /* 简化：总是从根目录开始 */
    
    while ((path = skipelem(path, name)) != 0) {
        ilock(ip);
        if (ip->type != T_DIR) {
            iunlockput(ip);
            return 0;
        }
        if (nameiparent && *path == '\0') {
            iunlock(ip);
            return ip;
        }
        if ((next = dirlookup(ip, name, 0)) == 0) {
            iunlockput(ip);
            return 0;
        }
        iunlockput(ip);
        ip = next;
    }
    
    if (nameiparent) {
        iput(ip);
        return 0;
    }
    return ip;
}

/* 解析路径返回 inode */
struct inode* namei(char *path) {
    char name[DIRSIZ];
    return namex(path, 0, name);
}
