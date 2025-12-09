/*
 * UART 串口驱动实现
 * 基于 16550 UART，适用于 QEMU virt 平台
 */

#include "../include/uart.h"

/* 寄存器访问宏 */
#define UART_REG(reg) (*(volatile unsigned char *)(UART0_BASE + (reg)))

/*
 * 初始化 UART
 */
void uart_init(void) {
    UART_REG(UART_IER) = 0x00;  /* 禁用中断 */
    UART_REG(UART_FCR) = 0x07;  /* 启用 FIFO */
    UART_REG(UART_LCR) = 0x03;  /* 8N1 */
}

/*
 * 输出单个字符
 */
void uart_putc(char c) {
    while ((UART_REG(UART_LSR) & UART_LSR_TX_IDLE) == 0)
        ;
    UART_REG(UART_THR) = c;
}

/*
 * 输出字符串
 */
void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

/*
 * 读取单个字符 (非阻塞)
 */
int uart_getc(void) {
    if (UART_REG(UART_LSR) & UART_LSR_RX_READY)
        return UART_REG(UART_RBR);
    return -1;
}
