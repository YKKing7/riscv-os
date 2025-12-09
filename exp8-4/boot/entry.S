/*
 * RISC-V 启动汇编代码
 * 功能：初始化运行环境，跳转到 C 主函数
 * 实验4扩展：支持中断处理与时钟管理
 *
 * 时钟中断处理流程：
 * 1. M 模式时钟中断由 timervec 处理
 * 2. timervec 设置下次时钟中断，并触发 S 模式软件中断
 * 3. S 模式在 kernelvec 中处理软件中断
 */

    .section .text.entry
    .global _start

/* CLINT 地址定义 */
.equ CLINT_BASE,     0x02000000
.equ CLINT_MTIMECMP, 0x4000
.equ CLINT_MTIME,    0xBFF8

/* 时钟中断间隔 (约 0.1 秒 @ 10MHz) */
.equ TIMER_INTERVAL, 1000000

_start:
    /* 保存 hartid */
    csrr    tp, mhartid
    
    /* 只让 hart 0 继续，其他 hart 等待 */
    bnez    tp, park
    
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

    # ============== 中断委托设置 ==============
    # 将中断和异常委托给 S 模式处理
    
    # 设置 medeleg - 异常委托寄存器
    # 委托以下异常给 S 模式:
    # - 指令地址未对齐 (0)
    # - 指令访问故障 (1)
    # - 非法指令 (2)
    # - 断点 (3)
    # - 加载地址未对齐 (4)
    # - 加载访问故障 (5)
    # - 存储地址未对齐 (6)
    # - 存储访问故障 (7)
    # - 用户模式环境调用 (8)
    # - 指令页故障 (12)
    # - 加载页故障 (13)
    # - 存储页故障 (15)
    li t0, 0xb1ff       # 位 0-8, 12, 13, 15
    csrw medeleg, t0
    
    # 设置 mideleg - 中断委托寄存器
    # 委托以下中断给 S 模式:
    # - S 模式软件中断 (1)
    # - S 模式外部中断 (9)
    # 注意：时钟中断 (7) 不委托，由 M 模式处理后转发
    li t0, 0x202        # 位 1, 9
    csrw mideleg, t0
    
    # ============== 设置 PMP (Physical Memory Protection) ==============
    # 允许 S 模式访问所有物理内存
    # 设置 pmpaddr0 为最大地址 (覆盖所有内存)
    li t0, 0x3fffffffffffffUL   # 2^54 - 1
    csrw pmpaddr0, t0
    
    # 设置 pmpcfg0: A=TOR (Top of Range), R=1, W=1, X=1
    # TOR 模式: 地址范围从 0 到 pmpaddr0
    # 配置字节: L=0, A=01 (TOR), X=1, W=1, R=1 = 0x0F
    li t0, 0x0f
    csrw pmpcfg0, t0
    
    # ============== 设置 S 模式计数器访问权限 ==============
    # 允许 S 模式读取 time 等计数器
    li t0, 0x2                  # 设置 mcounteren.TM 位
    csrw mcounteren, t0
    
    # ============== 设置 M 模式时钟中断 ==============
    
    # 保存 mscratch 区域地址 (用于 timervec)
    la t0, timer_scratch
    csrw mscratch, t0
    
    # 初始化 timer_scratch 区域
    # scratch[0] = mtimecmp 地址
    # scratch[1] = 时钟间隔
    li t1, CLINT_BASE + CLINT_MTIMECMP
    sd t1, 0(t0)
    li t1, TIMER_INTERVAL
    sd t1, 8(t0)
    
    # 设置第一次时钟中断
    li t1, CLINT_BASE + CLINT_MTIME
    ld t2, 0(t1)                # 读取当前时间
    li t3, TIMER_INTERVAL
    add t2, t2, t3              # 计算下次中断时间
    li t1, CLINT_BASE + CLINT_MTIMECMP
    sd t2, 0(t1)                # 设置 mtimecmp
    
    # 设置 M 模式中断向量
    la t0, timervec
    csrw mtvec, t0
    
    # 开启 M 模式时钟中断
    li t0, (1 << 7)             # MIE.MTIE
    csrs mie, t0
    
    # ============== 切换到 S 模式 ==============
    
    # 设置 mstatus.MPP = 01 (S 模式), mstatus.MIE = 1
    # MPP 在 mstatus 的位 11-12
    # MIE 在 mstatus 的位 3
    li t0, (1 << 11) | (1 << 3)
    csrw mstatus, t0
    
    # 设置 mepc 为 kernel_main 的地址
    la t0, kernel_main
    csrw mepc, t0
    
    # 使用 mret 跳转到 S 模式执行 kernel_main
    mret

# ============== M 模式时钟中断处理 ==============
# 
# 当 M 模式时钟中断发生时：
# 1. 设置下次时钟中断时间
# 2. 触发 S 模式软件中断 (通过设置 sip.SSIP)
# 3. 返回到被中断的代码
#
    .align 4
    .global timervec
timervec:
    # 保存寄存器到 mscratch 区域
    csrrw a0, mscratch, a0      # 交换 a0 和 mscratch
    sd a1, 16(a0)
    sd a2, 24(a0)
    sd a3, 32(a0)
    
    # 设置下次时钟中断
    # mtimecmp = mtime + interval
    ld a1, 0(a0)                # a1 = mtimecmp 地址
    ld a2, 8(a0)                # a2 = 时钟间隔
    ld a3, 0(a1)                # a3 = 当前 mtimecmp 值
    add a3, a3, a2              # a3 = 下次中断时间
    sd a3, 0(a1)                # 更新 mtimecmp
    
    # 触发 S 模式软件中断
    # 设置 sip.SSIP (位 1)
    li a1, 2
    csrs sip, a1
    
    # 恢复寄存器
    ld a3, 32(a0)
    ld a2, 24(a0)
    ld a1, 16(a0)
    csrrw a0, mscratch, a0      # 恢复 a0
    
    mret

/* 其他 hart 等待 */
park:
    wfi
    j       park

/* 如果 main 返回，进入死循环 */
halt:
    wfi
    j       halt

/* ============== 数据段 ============== */
    .section .bss
    .align 4
    
/* 栈空间定义 (16KB, 16字节对齐) */
    .global stack_top
stack:
    .skip   16384
stack_top:

/* M 模式时钟中断临时存储区域 */
    .align 4
timer_scratch:
    .skip   64
