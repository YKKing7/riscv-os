/*
 * printf 格式化输出实现
 * 
 * 支持的格式:
 *   %d, %ld  - 有符号十进制整数
 *   %u, %lu  - 无符号十进制整数
 *   %x, %lx  - 十六进制整数
 *   %p       - 指针地址
 *   %c       - 单个字符
 *   %s       - 字符串
 *   %%       - 百分号
 * 
 * 支持宽度格式: %6d, %10lu 等
 */

#include <stdarg.h>
#include "../include/types.h"
#include "../include/uart.h"

/* 数字字符表 */
static const char digits[] = "0123456789abcdef";

/*
 * 输出单个字符
 */
static void consputc(int c) {
    uart_putc(c);
}

/*
 * 打印指针 (带 0x 前缀)
 */
static void printptr(uint64 x) {
    consputc('0');
    consputc('x');
    for (int i = 60; i >= 0; i -= 4)
        consputc(digits[(x >> i) & 0xf]);
}

/* ============== 屏幕控制函数 ============== */

/*
 * 清屏
 */
void clear_screen(void) {
    uart_puts("\033[2J");  /* 清屏 */
    uart_puts("\033[H");   /* 光标移到左上角 */
}

/*
 * 清除当前行
 */
void clear_line(void) {
    uart_puts("\033[2K");  /* 清除整行 */
    uart_puts("\033[G");   /* 光标移到行首 */
}

/*
 * 光标定位
 * x: 列 (从1开始)
 * y: 行 (从1开始)
 */
void goto_xy(int x, int y) {
    /* 发送 ANSI 光标定位序列: ESC[y;xH */
    uart_putc('\033');
    uart_putc('[');
    /* 输出行号 */
    if (y >= 100) uart_putc('0' + y / 100);
    if (y >= 10) uart_putc('0' + (y / 10) % 10);
    uart_putc('0' + y % 10);
    uart_putc(';');
    /* 输出列号 */
    if (x >= 100) uart_putc('0' + x / 100);
    if (x >= 10) uart_putc('0' + (x / 10) % 10);
    uart_putc('0' + x % 10);
    uart_putc('H');
}

/* ============== 颜色控制函数 ============== */

/*
 * 设置输出颜色
 * color: ANSI 颜色代码 (30-37, 40-47, 90-97)
 */
void set_color(int color) {
    uart_putc('\033');
    uart_putc('[');
    if (color >= 100) uart_putc('0' + color / 100);
    if (color >= 10) uart_putc('0' + (color / 10) % 10);
    uart_putc('0' + color % 10);
    uart_putc('m');
}

/*
 * 重置颜色为默认
 */
void reset_color(void) {
    uart_puts("\033[0m");
}

/*
 * 打印带宽度的整数
 */
static void printint_width(int64 xx, int base, int sign, int width) {
    char buf[24];
    int i, len;
    uint64 x;
    int neg = 0;

    if (sign && xx < 0) {
        x = -xx;
        neg = 1;
    } else {
        x = xx;
    }

    i = 0;
    do {
        buf[i++] = digits[x % base];
        x /= base;
    } while (x != 0);

    len = i + neg;
    
    /* 填充空格 */
    while (width > len) {
        consputc(' ');
        width--;
    }

    if (neg)
        consputc('-');

    while (--i >= 0)
        consputc(buf[i]);
}

/*
 * 内部格式化输出函数
 */
static void vprintf_internal(const char *fmt, va_list ap) {
    int i, c;
    char *s;
    int width;
    int left_align;  /* 左对齐标志 */
    int len;

    for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
        if (c != '%') {
            consputc(c);
            continue;
        }
        
        c = fmt[++i] & 0xff;
        if (c == 0)
            break;
        
        /* 解析左对齐标志 */
        left_align = 0;
        if (c == '-') {
            left_align = 1;
            c = fmt[++i] & 0xff;
        }
        
        /* 解析宽度 */
        width = 0;
        while (c >= '0' && c <= '9') {
            width = width * 10 + (c - '0');
            c = fmt[++i] & 0xff;
        }
        if (c == 0)
            break;
            
        switch (c) {
        case 'd':
            printint_width(va_arg(ap, int), 10, 1, width);
            break;
        case 'u':
            printint_width(va_arg(ap, uint32), 10, 0, width);
            break;
        case 'x':
            printint_width(va_arg(ap, uint32), 16, 0, width);
            break;
        case 'l':
            c = fmt[++i] & 0xff;
            if (c == 'd')
                printint_width(va_arg(ap, int64), 10, 1, width);
            else if (c == 'u' || c == 'x')
                printint_width(va_arg(ap, uint64), c == 'u' ? 10 : 16, 0, width);
            break;
        case 'p':
            printptr(va_arg(ap, uint64));
            break;
        case 'c':
            consputc(va_arg(ap, int));
            break;
        case 's':
            s = va_arg(ap, char*);
            if (s == NULL)
                s = "(null)";
            /* 计算字符串长度 */
            len = 0;
            for (char *t = s; *t; t++) len++;
            /* 右对齐时先填充 */
            if (!left_align) {
                while (width > len) { consputc(' '); width--; }
            }
            /* 输出字符串 */
            while (*s)
                consputc(*s++);
            /* 左对齐时后填充 */
            if (left_align) {
                while (width > len) { consputc(' '); width--; }
            }
            break;
        case '%':
            consputc('%');
            break;
        default:
            consputc('%');
            consputc(c);
            break;
        }
    }
}

/*
 * 格式化输出
 */
int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf_internal(fmt, ap);
    va_end(ap);
    return 0;
}

/*
 * 带颜色的格式化输出
 */
int printf_color(int color, const char *fmt, ...) {
    va_list ap;
    set_color(color);
    va_start(ap, fmt);
    vprintf_internal(fmt, ap);
    va_end(ap);
    reset_color();
    return 0;
}
