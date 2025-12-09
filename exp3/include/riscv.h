/*
 * RISC-V 特权级相关定义
 * 功能：定义 CSR 寄存器操作、页表项格式等
 */

#ifndef RISCV_H
#define RISCV_H

#include "types.h"

/* ============== 页表项 (PTE) 标志位定义 ============== */

#define PTE_V   (1UL << 0)   /* Valid - 有效位 */
#define PTE_R   (1UL << 1)   /* Readable - 可读 */
#define PTE_W   (1UL << 2)   /* Writable - 可写 */
#define PTE_X   (1UL << 3)   /* Executable - 可执行 */
#define PTE_U   (1UL << 4)   /* User - 用户态可访问 */
#define PTE_G   (1UL << 5)   /* Global - 全局映射 */
#define PTE_A   (1UL << 6)   /* Accessed - 已访问 */
#define PTE_D   (1UL << 7)   /* Dirty - 已修改 */

/* ============== 页表项操作宏 ============== */

/* 从页表项提取物理地址 */
#define PTE2PA(pte)     (((pte) >> 10) << 12)
#define PTE_PA(pte)     PTE2PA(pte)  /* 别名，与文档保持一致 */

/* 从物理地址生成页表项（不含标志位） */
#define PA2PTE(pa)      ((((uint64)(pa)) >> 12) << 10)

/* 获取页表项的标志位 */
#define PTE_FLAGS(pte)  ((pte) & 0x3FF)

/* ============== Sv39 虚拟地址解析宏 ============== */

/*
 * Sv39 虚拟地址格式 (39位):
 * 
 *   38 ........ 30 | 29 ........ 21 | 20 ........ 12 | 11 ........ 0
 *       VPN[2]            VPN[1]            VPN[0]           offset
 *       (9位)             (9位)             (9位)            (12位)
 */

/* 从虚拟地址提取各级页表索引 */
#define VPN_SHIFT(level)    (12 + 9 * (level))
#define VPN_MASK            0x1FF   /* 9位掩码 */

/* 提取第 level 级的 VPN (level: 0, 1, 2) */
#define VA_VPN(va, level)   (((va) >> VPN_SHIFT(level)) & VPN_MASK)

/* 提取页内偏移 */
#define VA_OFFSET(va)       ((va) & 0xFFF)

/* ============== SATP 寄存器相关 ============== */

/*
 * SATP 寄存器格式 (Sv39):
 *   MODE[63:60] | ASID[59:44] | PPN[43:0]
 *   MODE = 8 表示 Sv39 模式
 */

#define SATP_SV39       (8UL << 60)

/* 从页表基地址生成 SATP 值 */
#define MAKE_SATP(pagetable)    (SATP_SV39 | (((uint64)(pagetable)) >> 12))

/* ============== CSR 寄存器读写操作 ============== */

/* 读取 SATP 寄存器 */
static inline uint64 r_satp(void) {
    uint64 x;
    __asm__ volatile("csrr %0, satp" : "=r"(x));
    return x;
}

/* 写入 SATP 寄存器 */
static inline void w_satp(uint64 x) {
    __asm__ volatile("csrw satp, %0" : : "r"(x));
}

/* ============== TLB 刷新操作 ============== */

/*
 * 刷新所有 TLB 条目
 */
static inline void sfence_vma(void) {
    __asm__ volatile("sfence.vma zero, zero");
}

/*
 * 刷新指定虚拟地址的 TLB 条目 (优化)
 */
static inline void sfence_vma_addr(uint64 va) {
    __asm__ volatile("sfence.vma %0, zero" : : "r"(va));
}

/*
 * 刷新指定 ASID 的所有 TLB 条目
 */
static inline void sfence_vma_asid(uint64 asid) {
    __asm__ volatile("sfence.vma zero, %0" : : "r"(asid));
}

/*
 * 刷新指定虚拟地址和 ASID 的 TLB 条目 (最精确)
 */
static inline void sfence_vma_addr_asid(uint64 va, uint64 asid) {
    __asm__ volatile("sfence.vma %0, %1" : : "r"(va), "r"(asid));
}

/* 读取 TP (Thread Pointer) 寄存器 - 用于获取 hartid */
static inline uint64 r_tp(void) {
    uint64 x;
    __asm__ volatile("mv %0, tp" : "=r"(x));
    return x;
}

/* ============== 内存屏障 ============== */

static inline void fence(void) {
    __asm__ volatile("fence" ::: "memory");
}

static inline void fence_i(void) {
    __asm__ volatile("fence.i" ::: "memory");
}

#endif /* RISCV_H */
