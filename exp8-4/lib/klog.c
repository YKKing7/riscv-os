/*
 * klog.c - 内核日志系统实现
 * 
 * 扩展实验8-4: 内核日志系统
 *   - 环形缓冲区管理
 *   - 格式化输出 (kvprintf)
 *   - 日志级别过滤
 *   - 中断安全的自旋锁
 */

#include "../include/klog.h"
#include "../include/riscv.h"

/* ============== 全局变量 ============== */

static struct klog_buffer log_buf;
static int current_log_level = LOG_LEVEL_INFO;

/* 日志级别名称 */
static const char *level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

/* 日志级别前缀 */
static const char *level_prefix[] = {
    "[DEBUG]", "[INFO]", "[WARN]", "[ERROR]", "[FATAL]"
};

/* ============== 自旋锁 (简化版，单核环境) ============== */

static void acquire_lock(volatile int *lock) {
    /* 单核环境下，简单的忙等待即可 */
    while (*lock)
        ;
    *lock = 1;
}

static void release_lock(volatile int *lock) {
    *lock = 0;
}

/* ============== 辅助函数 ============== */

static int klog_strlen(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static void klog_memcpy(char *dst, const char *src, int n) {
    while (n-- > 0)
        *dst++ = *src++;
}

/* 整数转字符串 (十进制) */
static int int_to_str(char *buf, int64 num, int is_signed) {
    char tmp[24];
    int i = 0, neg = 0;
    uint64 n;
    
    if (is_signed && num < 0) {
        neg = 1;
        n = -num;
    } else {
        n = num;
    }
    
    if (n == 0) {
        tmp[i++] = '0';
    } else {
        while (n > 0) {
            tmp[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    
    int len = 0;
    if (neg) buf[len++] = '-';
    while (i > 0) buf[len++] = tmp[--i];
    buf[len] = '\0';
    return len;
}

/* 整数转字符串 (十六进制) */
static int hex_to_str(char *buf, uint64 num, int uppercase) {
    char tmp[20];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    
    if (num == 0) {
        tmp[i++] = '0';
    } else {
        while (num > 0) {
            tmp[i++] = digits[num & 0xF];
            num >>= 4;
        }
    }
    
    int len = 0;
    while (i > 0) buf[len++] = tmp[--i];
    buf[len] = '\0';
    return len;
}

/* ============== 格式化输出 (kvprintf) ============== */

/* 简化版 vsnprintf，支持 %d, %u, %x, %X, %p, %s, %c, %% */
static int kvsnprintf(char *buf, int size, const char *fmt, __builtin_va_list ap) {
    int pos = 0;
    
    while (*fmt && pos < size - 1) {
        if (*fmt != '%') {
            buf[pos++] = *fmt++;
            continue;
        }
        
        fmt++;  /* 跳过 '%' */
        
        switch (*fmt) {
        case 'd':
        case 'i': {
            int val = __builtin_va_arg(ap, int);
            char tmp[24];
            int len = int_to_str(tmp, val, 1);
            for (int i = 0; i < len && pos < size - 1; i++)
                buf[pos++] = tmp[i];
            break;
        }
        case 'u': {
            unsigned int val = __builtin_va_arg(ap, unsigned int);
            char tmp[24];
            int len = int_to_str(tmp, val, 0);
            for (int i = 0; i < len && pos < size - 1; i++)
                buf[pos++] = tmp[i];
            break;
        }
        case 'x': {
            unsigned int val = __builtin_va_arg(ap, unsigned int);
            char tmp[20];
            int len = hex_to_str(tmp, val, 0);
            for (int i = 0; i < len && pos < size - 1; i++)
                buf[pos++] = tmp[i];
            break;
        }
        case 'X': {
            unsigned int val = __builtin_va_arg(ap, unsigned int);
            char tmp[20];
            int len = hex_to_str(tmp, val, 1);
            for (int i = 0; i < len && pos < size - 1; i++)
                buf[pos++] = tmp[i];
            break;
        }
        case 'p': {
            uint64 val = (uint64)__builtin_va_arg(ap, void*);
            buf[pos++] = '0';
            if (pos < size - 1) buf[pos++] = 'x';
            char tmp[20];
            int len = hex_to_str(tmp, val, 0);
            for (int i = 0; i < len && pos < size - 1; i++)
                buf[pos++] = tmp[i];
            break;
        }
        case 'l': {
            fmt++;
            if (*fmt == 'd' || *fmt == 'i') {
                int64 val = __builtin_va_arg(ap, int64);
                char tmp[24];
                int len = int_to_str(tmp, val, 1);
                for (int i = 0; i < len && pos < size - 1; i++)
                    buf[pos++] = tmp[i];
            } else if (*fmt == 'u') {
                uint64 val = __builtin_va_arg(ap, uint64);
                char tmp[24];
                int len = int_to_str(tmp, val, 0);
                for (int i = 0; i < len && pos < size - 1; i++)
                    buf[pos++] = tmp[i];
            } else if (*fmt == 'x') {
                uint64 val = __builtin_va_arg(ap, uint64);
                char tmp[20];
                int len = hex_to_str(tmp, val, 0);
                for (int i = 0; i < len && pos < size - 1; i++)
                    buf[pos++] = tmp[i];
            }
            break;
        }
        case 's': {
            const char *s = __builtin_va_arg(ap, const char*);
            if (s == 0) s = "(null)";
            while (*s && pos < size - 1)
                buf[pos++] = *s++;
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            buf[pos++] = c;
            break;
        }
        case '%':
            buf[pos++] = '%';
            break;
        default:
            buf[pos++] = '%';
            if (pos < size - 1) buf[pos++] = *fmt;
            break;
        }
        fmt++;
    }
    
    buf[pos] = '\0';
    return pos;
}

/* ============== 环形缓冲区操作 ============== */

/* 写入数据到环形缓冲区 */
static void klog_write(const char *data, int len) {
    acquire_lock(&log_buf.lock);
    
    for (int i = 0; i < len; i++) {
        log_buf.buf[log_buf.write_pos] = data[i];
        log_buf.write_pos = (log_buf.write_pos + 1) % LOG_BUF_SIZE;
        
        if (log_buf.count < LOG_BUF_SIZE) {
            log_buf.count++;
        } else {
            /* 缓冲区满，覆盖旧数据，移动读指针 */
            log_buf.read_pos = (log_buf.read_pos + 1) % LOG_BUF_SIZE;
        }
    }
    
    release_lock(&log_buf.lock);
}

/* ============== 公共接口 ============== */

/* 初始化日志系统 */
void klog_init(void) {
    log_buf.lock = 0;
    log_buf.read_pos = 0;
    log_buf.write_pos = 0;
    log_buf.count = 0;
    log_buf.total_logs = 0;
    log_buf.dropped_logs = 0;
    current_log_level = LOG_LEVEL_INFO;
}

/* 核心日志函数 */
void klog(int level, const char *fmt, ...) {
    /* 级别过滤 */
    if (level < current_log_level) {
        return;
    }
    
    char formatted[MAX_LOG_LEN];
    int pos = 0;
    
    /* 添加级别前缀 */
    if (level >= 0 && level <= LOG_LEVEL_FATAL) {
        const char *prefix = level_prefix[level];
        while (*prefix && pos < MAX_LOG_LEN - 1)
            formatted[pos++] = *prefix++;
        formatted[pos++] = ' ';
    }
    
    /* 格式化用户消息 */
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    pos += kvsnprintf(formatted + pos, MAX_LOG_LEN - pos, fmt, ap);
    __builtin_va_end(ap);
    
    /* 添加换行符 */
    if (pos < MAX_LOG_LEN - 1) {
        formatted[pos++] = '\n';
    }
    formatted[pos] = '\0';
    
    /* 写入缓冲区 */
    klog_write(formatted, pos);
    
    /* 更新统计 */
    acquire_lock(&log_buf.lock);
    log_buf.total_logs++;
    release_lock(&log_buf.lock);
}

/* 设置日志级别 */
void klog_set_level(int level) {
    if (level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_FATAL) {
        current_log_level = level;
    }
}

/* 获取当前日志级别 */
int klog_get_level(void) {
    return current_log_level;
}

/* 获取日志级别名称 */
const char* klog_level_name(int level) {
    if (level >= 0 && level <= LOG_LEVEL_FATAL) {
        return level_names[level];
    }
    return "UNKNOWN";
}

/* 读取日志 (系统调用接口) */
int sys_klog_read(char *buf, int n) {
    if (buf == 0 || n <= 0) {
        return -1;
    }
    
    acquire_lock(&log_buf.lock);
    
    int bytes_to_read = log_buf.count;
    if (bytes_to_read == 0) {
        release_lock(&log_buf.lock);
        return 0;
    }
    
    if (n < bytes_to_read) {
        bytes_to_read = n;
    }
    
    /* 从环形缓冲区读取数据 */
    int read_pos = log_buf.read_pos;
    for (int i = 0; i < bytes_to_read; i++) {
        buf[i] = log_buf.buf[read_pos];
        read_pos = (read_pos + 1) % LOG_BUF_SIZE;
    }
    
    /* 更新读指针和计数 */
    log_buf.read_pos = read_pos;
    log_buf.count -= bytes_to_read;
    
    release_lock(&log_buf.lock);
    
    return bytes_to_read;
}

/* 获取日志统计信息 */
struct klog_stats klog_get_stats(void) {
    struct klog_stats stats;
    
    acquire_lock(&log_buf.lock);
    stats.total_logs = log_buf.total_logs;
    stats.dropped_logs = log_buf.dropped_logs;
    stats.current_level = current_log_level;
    stats.buffer_used = log_buf.count;
    stats.buffer_size = LOG_BUF_SIZE;
    release_lock(&log_buf.lock);
    
    return stats;
}

/* 清空日志缓冲区 */
void klog_clear(void) {
    acquire_lock(&log_buf.lock);
    log_buf.read_pos = 0;
    log_buf.write_pos = 0;
    log_buf.count = 0;
    release_lock(&log_buf.lock);
}
