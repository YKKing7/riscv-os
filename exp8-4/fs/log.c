/*
 * log.c - 写前日志 (Write-Ahead Logging)
 * 
 * 功能：
 *   - 事务原子性保证
 *   - 崩溃恢复支持
 *   - 日志聚合优化
 * 
 * 原理：
 *   1. begin_op: 开始事务
 *   2. log_write: 记录修改到日志
 *   3. end_op: 提交事务
 *   4. 崩溃后通过日志回放恢复
 */

#include "../include/types.h"
#include "../include/buf.h"
#include "../include/fs.h"
#include "../include/printf.h"

/* ============== 日志常量 ============== */

#define LOGSIZE     30      /* 日志区最大块数 */
#define MAXOPBLOCKS 10      /* 单次操作最大修改块数 */

/* ============== 日志结构 ============== */

/* 日志头 (存储在日志区第一块) */
struct logheader {
    int n;                  /* 日志中的块数量 */
    int block[LOGSIZE];     /* 每个日志块对应的目标块号 */
};

/* 日志状态 */
static struct {
    int start;              /* 日志区起始块号 */
    int size;               /* 日志区大小 */
    int outstanding;        /* 正在进行的操作数 */
    int committing;         /* 是否正在提交 */
    int dev;                /* 设备号 */
    struct logheader lh;    /* 内存中的日志头 */
} log;

/* ============== 内部函数声明 ============== */

static void recover_from_log(void);
static void commit(void);
static void write_log(void);
static void write_head(void);
static void read_head(void);
static void install_trans(int recovering);

/* ============== 日志初始化 ============== */

void initlog(int dev, struct superblock *sb) {
    log.start = sb->logstart;
    log.size = sb->nlog;
    log.dev = dev;
    log.outstanding = 0;
    log.committing = 0;
    log.lh.n = 0;
    
    recover_from_log();
}

/* ============== 事务接口 ============== */

/* 开始文件系统操作 */
void begin_op(void) {
    /* 等待提交完成或日志空间足够 */
    while (log.committing || 
           log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGSIZE) {
        /* 在真实系统中应该 sleep */
    }
    log.outstanding++;
}

/* 结束文件系统操作 */
void end_op(void) {
    int do_commit = 0;
    
    log.outstanding--;
    if (log.committing) {
        printf("end_op: log is committing\n");
        return;
    }
    
    if (log.outstanding == 0) {
        do_commit = 1;
        log.committing = 1;
    }
    
    if (do_commit) {
        commit();
        log.committing = 0;
    }
}

/* 记录块修改到日志 */
void log_write(struct buf *b) {
    int i;
    
    if (log.lh.n >= LOGSIZE) {
        printf("log_write: transaction too big\n");
        return;
    }
    if (log.outstanding < 1) {
        printf("log_write: outside of transaction\n");
        return;
    }
    
    /* 检查是否已在日志中 (日志吸收) */
    for (i = 0; i < log.lh.n; i++) {
        if (log.lh.block[i] == (int)b->blockno)
            break;
    }
    
    log.lh.block[i] = b->blockno;
    if (i == log.lh.n) {
        bpin(b);
        log.lh.n++;
    }
}

/* ============== 内部实现 ============== */

/* 提交事务 */
static void commit(void) {
    if (log.lh.n > 0) {
        write_log();        /* 将修改块写入日志区 */
        write_head();       /* 写入日志头 (真正的提交点) */
        install_trans(0);   /* 将日志内容写入目标位置 */
        log.lh.n = 0;
        write_head();       /* 清空日志 */
    }
}

/* 将修改块从缓存写入日志区 */
static void write_log(void) {
    for (int tail = 0; tail < log.lh.n; tail++) {
        struct buf *to = bread(log.dev, log.start + tail + 1);
        struct buf *from = bread(log.dev, log.lh.block[tail]);
        
        for (int i = 0; i < BSIZE; i++)
            to->data[i] = from->data[i];
        
        bwrite(to);
        brelse(from);
        brelse(to);
    }
}

/* 写入日志头到磁盘 */
static void write_head(void) {
    struct buf *buf = bread(log.dev, log.start);
    struct logheader *hb = (struct logheader*)buf->data;
    
    hb->n = log.lh.n;
    for (int i = 0; i < log.lh.n; i++)
        hb->block[i] = log.lh.block[i];
    
    bwrite(buf);
    brelse(buf);
}

/* 从磁盘读取日志头 */
static void read_head(void) {
    struct buf *buf = bread(log.dev, log.start);
    struct logheader *lh = (struct logheader*)buf->data;
    
    log.lh.n = lh->n;
    for (int i = 0; i < log.lh.n; i++)
        log.lh.block[i] = lh->block[i];
    
    brelse(buf);
}

/* 将日志内容安装到目标位置 */
static void install_trans(int recovering) {
    for (int tail = 0; tail < log.lh.n; tail++) {
        struct buf *lbuf = bread(log.dev, log.start + tail + 1);
        struct buf *dbuf = bread(log.dev, log.lh.block[tail]);
        
        if (recovering)
            printf("  recovering block %d\n", log.lh.block[tail]);
        
        for (int i = 0; i < BSIZE; i++)
            dbuf->data[i] = lbuf->data[i];
        
        bwrite(dbuf);
        if (!recovering)
            bunpin(dbuf);
        
        brelse(lbuf);
        brelse(dbuf);
    }
}

/* 崩溃恢复 */
static void recover_from_log(void) {
    read_head();
    if (log.lh.n > 0) {
        printf("  Recovering %d blocks from log...\n", log.lh.n);
        install_trans(1);
        log.lh.n = 0;
        write_head();
    }
}

/* ============== 调试接口 ============== */

void debug_log_state(void) {
    printf("  Log: start=%d, size=%d, outstanding=%d, n=%d\n",
           log.start, log.size, log.outstanding, log.lh.n);
}
