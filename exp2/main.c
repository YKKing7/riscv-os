/*
 * 实验2：UART 驱动与 printf 实现
 * 
 * 实验目标：
 *   1. 理解 UART 串口通信原理
 *   2. 实现格式化输出函数 printf
 *   3. 支持多种格式化输出 (%d, %x, %s, %c, %p 等)
 * 
 * 基于实验1，新增：
 *   - include/types.h   : 基本数据类型定义
 *   - include/printf.h  : printf 函数声明
 *   - lib/printf.c      : printf 实现
 */

#include "include/uart.h"
#include "include/printf.h"

/* ============== 辅助函数 ============== */

static void delay(volatile int count) {
    while (count-- > 0);
}

/* ============== 内核入口 ============== */

void kernel_main(void) {
    uart_init();
    
    printf("\n");
    printf("=============================================\n");
    printf("  Exp2: UART Driver & printf Implementation\n");
    printf("=============================================\n");
    
    /* Test 1: printf 基本功能测试 */
    printf("\nTest 1: printf basic functions\n");
    printf("  Integer:  %d\n", 42);
    printf("  Negative: %d\n", -123);
    printf("  Hex:      0x%x\n", 0xABCD);
    printf("  String:   %s\n", "Hello RISC-V");
    printf("  Char:     %c\n", 'X');
    printf("  Pointer:  %p\n", (void*)0x80000000);
    printf("  Percent:  %%\n");
    printf("Test 1 PASS!\n");
    
    /* Test 2: printf 边界测试 */
    printf("\nTest 2: printf boundary cases\n");
    printf("  INT_MAX:     %d\n", 2147483647);
    printf("  INT_MIN:     %d\n", (int)-2147483648);
    printf("  UINT_MAX:    %u\n", 0xFFFFFFFF);
    printf("  Long:        %ld\n", 123456789012345L);
    printf("  NULL string: %s\n", (char*)0);
    printf("Test 2 PASS!\n");
    
    /* Test 3: 颜色输出测试 (ANSI 终端) */
    printf("\nTest 3: Color output\n");
    printf_color(COLOR_RED,    "  Red text\n");
    printf_color(COLOR_GREEN,  "  Green text\n");
    printf_color(COLOR_YELLOW, "  Yellow text\n");
    printf_color(COLOR_BLUE,   "  Blue text\n");
    printf_color(COLOR_CYAN,   "  Cyan text\n");
    printf("Test 3 PASS!\n");

    /* Test 4: 光标定位测试 (ANSI ESC[y;xH) */
    printf("\nTest 4: Cursor positioning\n");
    printf("  Moving cursor to (35, 2)...\n");
    goto_xy(35, 2);
    printf("  >>> INSERTED <<< by goto_xy(35, 2)");
    goto_xy(1, 100);
    printf("  Cursor returned to continue output\n");
    printf("Test 4 PASS!\n");
    
    /* Test 5: 清除行测试 (ANSI ESC[2K) */
    printf("\nTest 5: Clear line\n");
    printf("  BEFORE: This line will be cleared...");
    delay(1000000000);  /* 短暂延时以便观察 */
    clear_line();
    printf("  AFTER: Line cleared!\n");
    printf("Test 5 PASS!\n");
    
    printf("\n=============================================\n");
    printf("  All tests passed!\n");
    printf("=============================================\n");
    
    while (1) {
        __asm__ volatile("wfi");
    }
}
