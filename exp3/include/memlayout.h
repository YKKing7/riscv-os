/*
 * 内存布局定义
 * 功能：定义 QEMU virt 平台的内存地址映射
 */

#ifndef MEMLAYOUT_H
#define MEMLAYOUT_H

/* ============== QEMU virt 平台内存布局 ============== */

/* UART 串口设备地址 */
#define UART0           0x10000000UL
#define UART0_SIZE      0x1000UL

/* CLINT (Core Local Interruptor) - 定时器和软中断 */
#define CLINT           0x02000000UL
#define CLINT_SIZE      0x10000UL

/* PLIC (Platform-Level Interrupt Controller) */
#define PLIC            0x0C000000UL
#define PLIC_SIZE       0x4000000UL

/* 物理内存起始地址 (QEMU virt 标准) */
#define KERNBASE        0x80000000UL

/* 物理内存结束地址 (假设 128MB 内存) */
#define PHYSTOP         (KERNBASE + 128 * 1024 * 1024)

/* ============== 页面相关常量 ============== */

/* 页面大小：4KB */
#define PGSIZE          4096
#define PGSHIFT         12

/* 页面对齐宏 */
#define PGROUNDUP(sz)   (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(a)  ((a) & ~(PGSIZE - 1))

/* ============== Sv39 页表相关常量 ============== */

/* 页表项数量：每级页表 512 项 (2^9) */
#define NPTENTRIES      512

/* 最大虚拟地址 (Sv39: 39位) */
#define MAXVA           (1UL << 38)

#endif /* MEMLAYOUT_H */
