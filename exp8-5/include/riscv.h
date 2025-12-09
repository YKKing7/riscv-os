/*
 * riscv.h - RISC-V 特权级定义 (CSR/中断/页表)
 */

#ifndef RISCV_H
#define RISCV_H

#include "types.h"

/* ============== 中断编号 ============== */

#define IRQ_S_SOFT      1   /* S模式软件中断 */
#define IRQ_S_TIMER     5   /* S模式时钟中断 */
#define IRQ_S_EXT       9   /* S模式外部中断 */

/* ============== 异常编号 ============== */

#define EXC_INST_MISALIGN   0
#define EXC_INST_FAULT      1
#define EXC_ILLEGAL_INST    2
#define EXC_BREAKPOINT      3
#define EXC_LOAD_MISALIGN   4
#define EXC_LOAD_FAULT      5
#define EXC_STORE_MISALIGN  6
#define EXC_STORE_FAULT     7
#define EXC_ECALL_U         8
#define EXC_ECALL_S         9
#define EXC_INST_PAGE_FAULT 12
#define EXC_LOAD_PAGE_FAULT 13
#define EXC_STORE_PAGE_FAULT 15

#define SCAUSE_INTERRUPT    (1UL << 63)

/* ============== sstatus/sie 位定义 ============== */

#define SSTATUS_SIE     (1UL << 1)
#define SSTATUS_SPIE    (1UL << 5)
#define SSTATUS_SPP     (1UL << 8)

#define SIE_SSIE        (1UL << 1)
#define SIE_STIE        (1UL << 5)
#define SIE_SEIE        (1UL << 9)

#define SIP_SSIP        (1UL << 1)
#define SIP_STIP        (1UL << 5)
#define SIP_SEIP        (1UL << 9)

/* ============== 页表项 (PTE) ============== */

#define PTE_V   (1UL << 0)  /* Valid */
#define PTE_R   (1UL << 1)  /* Readable */
#define PTE_W   (1UL << 2)  /* Writable */
#define PTE_X   (1UL << 3)  /* Executable */
#define PTE_U   (1UL << 4)  /* User accessible */
#define PTE_G   (1UL << 5)  /* Global */
#define PTE_A   (1UL << 6)  /* Accessed */
#define PTE_D   (1UL << 7)  /* Dirty */
#define PTE_C   (1UL << 8)  /* COW (Copy-on-Write) - RSW bit */

#define PTE2PA(pte)     (((pte) >> 10) << 12)
#define PA2PTE(pa)      ((((uint64)(pa)) >> 12) << 10)
#define PTE_FLAGS(pte)  ((pte) & 0x3FF)

/* ============== Sv39 虚拟地址 ============== */

#define VPN_SHIFT(level)    (12 + 9 * (level))
#define VPN_MASK            0x1FF
#define VA_VPN(va, level)   (((va) >> VPN_SHIFT(level)) & VPN_MASK)
#define VA_OFFSET(va)       ((va) & 0xFFF)

/* ============== SATP ============== */

#define SATP_SV39           (8UL << 60)
#define MAKE_SATP(pt)       (SATP_SV39 | (((uint64)(pt)) >> 12))

/* ============== CSR 操作 ============== */

static inline uint64 r_satp(void)    { uint64 x; __asm__ volatile("csrr %0, satp" : "=r"(x)); return x; }
static inline void   w_satp(uint64 x){ __asm__ volatile("csrw satp, %0" : : "r"(x)); }
static inline uint64 r_sstatus(void) { uint64 x; __asm__ volatile("csrr %0, sstatus" : "=r"(x)); return x; }
static inline void   w_sstatus(uint64 x) { __asm__ volatile("csrw sstatus, %0" : : "r"(x)); }
static inline uint64 r_sie(void)     { uint64 x; __asm__ volatile("csrr %0, sie" : "=r"(x)); return x; }
static inline void   w_sie(uint64 x) { __asm__ volatile("csrw sie, %0" : : "r"(x)); }
static inline uint64 r_sip(void)     { uint64 x; __asm__ volatile("csrr %0, sip" : "=r"(x)); return x; }
static inline void   w_sip(uint64 x) { __asm__ volatile("csrw sip, %0" : : "r"(x)); }
static inline uint64 r_sepc(void)    { uint64 x; __asm__ volatile("csrr %0, sepc" : "=r"(x)); return x; }
static inline void   w_sepc(uint64 x){ __asm__ volatile("csrw sepc, %0" : : "r"(x)); }
static inline uint64 r_scause(void)  { uint64 x; __asm__ volatile("csrr %0, scause" : "=r"(x)); return x; }
static inline uint64 r_stval(void)   { uint64 x; __asm__ volatile("csrr %0, stval" : "=r"(x)); return x; }
static inline uint64 r_stvec(void)   { uint64 x; __asm__ volatile("csrr %0, stvec" : "=r"(x)); return x; }
static inline void   w_stvec(uint64 x) { __asm__ volatile("csrw stvec, %0" : : "r"(x)); }
static inline uint64 r_time(void)    { uint64 x; __asm__ volatile("csrr %0, time" : "=r"(x)); return x; }
static inline uint64 r_tp(void)      { uint64 x; __asm__ volatile("mv %0, tp" : "=r"(x)); return x; }

/* ============== TLB/屏障 ============== */

static inline void sfence_vma(void) { __asm__ volatile("sfence.vma zero, zero"); }
static inline void sfence_vma_addr(uint64 va) { __asm__ volatile("sfence.vma %0, zero" : : "r"(va)); }
static inline void fence(void)   { __asm__ volatile("fence" ::: "memory"); }
static inline void fence_i(void) { __asm__ volatile("fence.i" ::: "memory"); }

/* ============== 中断控制 ============== */

static inline void intr_on(void)  { w_sstatus(r_sstatus() | SSTATUS_SIE); }
static inline void intr_off(void) { w_sstatus(r_sstatus() & ~SSTATUS_SIE); }
static inline int  intr_get(void) { return (r_sstatus() & SSTATUS_SIE) != 0; }

#endif /* RISCV_H */
