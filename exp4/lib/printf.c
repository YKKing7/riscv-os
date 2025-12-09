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
 * 打印整数
 * xx: 要打印的数值
 * base: 进制 (10 或 16)
 * sign: 是否有符号
 */
static void printint(int64 xx, int base, int sign) {
    char buf[24];
    int i;
    uint64 x;

    if (sign && xx < 0) {
        x = -xx;
        sign = 1;
    } else {
        x = xx;
        sign = 0;
    }

    i = 0;
    do {
        buf[i++] = digits[x % base];
        x /= base;
    } while (x != 0);

    if (sign)
        buf[i++] = '-';

    while (--i >= 0)
        consputc(buf[i]);
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
 * 内部格式化输出函数
 */
static void vprintf_internal(const char *fmt, va_list ap) {
    int i, c;
    char *s;

    for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
        if (c != '%') {
            consputc(c);
            continue;
        }
        
        c = fmt[++i] & 0xff;
        if (c == 0)
            break;
            
        switch (c) {
        case 'd':
            printint(va_arg(ap, int), 10, 1);
            break;
        case 'u':
            printint(va_arg(ap, uint32), 10, 0);
            break;
        case 'x':
            printint(va_arg(ap, uint32), 16, 0);
            break;
        case 'l':
            c = fmt[++i] & 0xff;
            if (c == 'd')
                printint(va_arg(ap, int64), 10, 1);
            else if (c == 'u' || c == 'x')
                printint(va_arg(ap, uint64), c == 'u' ? 10 : 16, 0);
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
            while (*s)
                consputc(*s++);
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
