/*
 * UART 串口驱动头文件
 * 基于 16550 UART，适用于 QEMU virt 平台
 */

#ifndef UART_H
#define UART_H

/* UART0 硬件基地址 */
#define UART0_BASE  0x10000000UL

/* 16550 UART 寄存器偏移 */
#define UART_THR    0   /* 发送保持寄存器 (写) */
#define UART_RBR    0   /* 接收缓冲寄存器 (读) */
#define UART_IER    1   /* 中断使能寄存器 */
#define UART_FCR    2   /* FIFO 控制寄存器 (写) */
#define UART_LCR    3   /* 线路控制寄存器 */
#define UART_LSR    5   /* 线路状态寄存器 */

/* LSR 寄存器位定义 */
#define UART_LSR_RX_READY   (1 << 0)  /* 接收数据就绪 */
#define UART_LSR_TX_IDLE    (1 << 5)  /* 发送保持寄存器空 */

/* 函数声明 */
void uart_init(void);           /* 初始化 UART */
void uart_putc(char c);         /* 输出单个字符 */
void uart_puts(const char *s);  /* 输出字符串 */
int  uart_getc(void);           /* 读取单个字符 */

#endif
