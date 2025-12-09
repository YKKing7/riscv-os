/*
 * RISC-V 启动汇编代码
 * 功能：初始化运行环境，跳转到 C 主函数
 */

    .section .text.entry
    .global _start

_start:
    /* 设置栈指针 (16字节对齐) */
    la      sp, stack_top
    
    /* 清零 BSS 段 */
    la      t0, _sbss
    la      t1, _ebss
    bgeu    t0, t1, 2f
1:
    sd      zero, 0(t0)
    addi    t0, t0, 8
    bltu    t0, t1, 1b
2:
    /* 跳转到 C 主函数 */
    call    kernel_main

    /* 如果 main 返回，进入死循环 */
halt:
    wfi
    j       halt

/* 栈空间定义 (4KB, 16字节对齐) */
    .section .bss
    .align 4
    .global stack_top
stack:
    .skip   4096
stack_top:
