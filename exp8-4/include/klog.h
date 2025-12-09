/*
 * klog.h - 内核日志系统
 * 
 * 扩展实验8-4: 内核日志系统
 *   - 结构化日志级别 (DEBUG/INFO/WARN/ERROR/FATAL)
 *   - 高性能环形缓冲区
 *   - 可变参数格式化输出
 *   - 运行时日志级别控制
 */

#ifndef KLOG_H
#define KLOG_H

#include "types.h"

/* ============== 日志级别 ============== */

#define LOG_LEVEL_DEBUG 0   /* 详细调试信息 */
#define LOG_LEVEL_INFO  1   /* 常规运行信息 */
#define LOG_LEVEL_WARN  2   /* 潜在问题警告 */
#define LOG_LEVEL_ERROR 3   /* 错误但可继续运行 */
#define LOG_LEVEL_FATAL 4   /* 严重错误 */

/* ============== 缓冲区参数 ============== */

#define LOG_BUF_SIZE    4096    /* 环形缓冲区大小 */
#define MAX_LOG_LEN     256     /* 单条日志最大长度 */

/* ============== 日志缓冲区结构 ============== */

struct klog_buffer {
    volatile int lock;          /* 自旋锁 */
    char buf[LOG_BUF_SIZE];     /* 环形缓冲区 */
    int read_pos;               /* 读取位置 */
    int write_pos;              /* 写入位置 */
    int count;                  /* 当前数据量 */
    uint64 total_logs;          /* 总日志条数 */
    uint64 dropped_logs;        /* 丢弃的日志条数 */
};

/* ============== 日志统计 ============== */

struct klog_stats {
    uint64 total_logs;          /* 总日志条数 */
    uint64 dropped_logs;        /* 丢弃的日志条数 */
    int current_level;          /* 当前日志级别 */
    int buffer_used;            /* 缓冲区已用字节数 */
    int buffer_size;            /* 缓冲区总大小 */
};

/* ============== 接口函数 ============== */

/* 初始化日志系统 */
void klog_init(void);

/* 核心日志函数 */
void klog(int level, const char *fmt, ...);

/* 日志级别控制 */
void klog_set_level(int level);
int  klog_get_level(void);
const char* klog_level_name(int level);

/* 读取日志 (供用户空间使用) */
int sys_klog_read(char *buf, int n);

/* 获取日志统计信息 */
struct klog_stats klog_get_stats(void);

/* 清空日志缓冲区 */
void klog_clear(void);

/* 便捷宏 */
#define KLOG_DEBUG(fmt, ...) klog(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define KLOG_INFO(fmt, ...)  klog(LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__)
#define KLOG_WARN(fmt, ...)  klog(LOG_LEVEL_WARN,  fmt, ##__VA_ARGS__)
#define KLOG_ERROR(fmt, ...) klog(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define KLOG_FATAL(fmt, ...) klog(LOG_LEVEL_FATAL, fmt, ##__VA_ARGS__)

#endif /* KLOG_H */
