/*
 * 实验1：RISC-V 最小内核
 * 
 * 实验目标：
 *   1. 理解 RISC-V 启动流程
 *   2. 实现最小可运行内核
 *   3. 通过 UART 输出验证系统运行
 * 
 * 本实验文件：
 *   - boot/entry.S     : 汇编启动代码
 *   - drivers/uart.c   : UART 驱动实现
 *   - include/uart.h   : UART 接口声明
 *   - linker/kernel.ld : 链接脚本
 */

#include "include/uart.h"

/* ============== 内核入口 ============== */

void kernel_main(void) {
    uart_init();
    
    uart_puts("\n");
    uart_puts("=============================================\n");
    uart_puts("  Exp1: RISC-V Minimal Kernel\n");
    uart_puts("=============================================\n");
    uart_puts("\n");
    uart_puts("  Hello OS!\n");
    uart_puts("  UART initialized successfully.\n");
    uart_puts("\n");
    uart_puts("=============================================\n");
    uart_puts("  System running!\n");
    uart_puts("=============================================\n");
    
    while (1) {
        __asm__ volatile("wfi");
    }
}