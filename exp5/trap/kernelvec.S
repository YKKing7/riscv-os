/*
 * 内核中断向量入口
 * 实验4：中断处理与时钟管理
 * 功能：保存/恢复上下文，调用 C 语言中断处理函数
 */

    .section .text
    .global kernelvec
    .align 4

/*
 * kernelvec - 内核态中断/异常入口
 * 
 * 当 S 模式发生中断或异常时，硬件会：
 * 1. 将 pc 保存到 sepc
 * 2. 将当前特权级保存到 sstatus.SPP
 * 3. 将中断使能位保存到 sstatus.SPIE，并清除 sstatus.SIE
 * 4. 将异常原因写入 scause
 * 5. 将相关值写入 stval
 * 6. 跳转到 stvec 指向的地址
 * 
 * 我们需要：
 * 1. 保存所有通用寄存器到栈上
 * 2. 调用 C 语言处理函数 kerneltrap()
 * 3. 恢复所有通用寄存器
 * 4. 使用 sret 返回
 */
kernelvec:
    # 在当前内核栈上分配空间保存上下文
    # 需要保存 31 个通用寄存器 (x1-x31) + sepc + sstatus = 33 * 8 = 264 字节
    # 为了 16 字节对齐，分配 272 字节
    addi sp, sp, -272

    # 保存通用寄存器 (x1-x31)
    # x0 (zero) 不需要保存，始终为 0
    sd ra,   0(sp)      # x1  - 返回地址
    sd sp,   8(sp)      # x2  - 栈指针 (保存调整前的值需要特殊处理)
    sd gp,  16(sp)      # x3  - 全局指针
    sd tp,  24(sp)      # x4  - 线程指针
    sd t0,  32(sp)      # x5  - 临时寄存器
    sd t1,  40(sp)      # x6  - 临时寄存器
    sd t2,  48(sp)      # x7  - 临时寄存器
    sd s0,  56(sp)      # x8  - 保存寄存器/帧指针
    sd s1,  64(sp)      # x9  - 保存寄存器
    sd a0,  72(sp)      # x10 - 函数参数/返回值
    sd a1,  80(sp)      # x11 - 函数参数/返回值
    sd a2,  88(sp)      # x12 - 函数参数
    sd a3,  96(sp)      # x13 - 函数参数
    sd a4, 104(sp)      # x14 - 函数参数
    sd a5, 112(sp)      # x15 - 函数参数
    sd a6, 120(sp)      # x16 - 函数参数
    sd a7, 128(sp)      # x17 - 函数参数
    sd s2, 136(sp)      # x18 - 保存寄存器
    sd s3, 144(sp)      # x19 - 保存寄存器
    sd s4, 152(sp)      # x20 - 保存寄存器
    sd s5, 160(sp)      # x21 - 保存寄存器
    sd s6, 168(sp)      # x22 - 保存寄存器
    sd s7, 176(sp)      # x23 - 保存寄存器
    sd s8, 184(sp)      # x24 - 保存寄存器
    sd s9, 192(sp)      # x25 - 保存寄存器
    sd s10, 200(sp)     # x26 - 保存寄存器
    sd s11, 208(sp)     # x27 - 保存寄存器
    sd t3, 216(sp)      # x28 - 临时寄存器
    sd t4, 224(sp)      # x29 - 临时寄存器
    sd t5, 232(sp)      # x30 - 临时寄存器
    sd t6, 240(sp)      # x31 - 临时寄存器

    # 保存 CSR 寄存器
    csrr t0, sepc
    sd t0, 248(sp)      # sepc - 异常程序计数器
    csrr t0, sstatus
    sd t0, 256(sp)      # sstatus - 状态寄存器

    # 修正保存的 sp 值 (保存调整前的栈指针)
    addi t0, sp, 272
    sd t0, 8(sp)

    # 调用 C 语言中断处理函数
    # kerneltrap 会根据需要修改 sepc (如异常处理时跳过指令)
    call kerneltrap

    # 恢复 sstatus (sepc 已由 kerneltrap 设置，不需要恢复)
    ld t0, 256(sp)
    csrw sstatus, t0    # 恢复 sstatus

    # 恢复通用寄存器
    ld ra,   0(sp)
    # sp 稍后恢复
    ld gp,  16(sp)
    ld tp,  24(sp)
    ld t0,  32(sp)
    ld t1,  40(sp)
    ld t2,  48(sp)
    ld s0,  56(sp)
    ld s1,  64(sp)
    ld a0,  72(sp)
    ld a1,  80(sp)
    ld a2,  88(sp)
    ld a3,  96(sp)
    ld a4, 104(sp)
    ld a5, 112(sp)
    ld a6, 120(sp)
    ld a7, 128(sp)
    ld s2, 136(sp)
    ld s3, 144(sp)
    ld s4, 152(sp)
    ld s5, 160(sp)
    ld s6, 168(sp)
    ld s7, 176(sp)
    ld s8, 184(sp)
    ld s9, 192(sp)
    ld s10, 200(sp)
    ld s11, 208(sp)
    ld t3, 216(sp)
    ld t4, 224(sp)
    ld t5, 232(sp)
    ld t6, 240(sp)

    # 恢复栈指针
    addi sp, sp, 272

    # 返回到中断/异常发生处
    sret

