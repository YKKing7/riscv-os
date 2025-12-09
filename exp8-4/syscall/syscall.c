/*
 * syscall.c - 系统调用分发机制
 * 
 * 功能：
 *   - 系统调用分发与路由 (syscall)
 *   - 参数提取 (argint/argaddr/argstr)
 *   - 用户内存访问 (copyin/copyout/copyinstr)
 *   - 指针安全验证 (check_user_ptr)
 */

#include "../include/syscall.h"
#include "../include/proc.h"
#include "../include/printf.h"

/* ============== 调试支持 ============== */

static int syscall_trace = 0;

void enable_syscall_trace(int enable) { syscall_trace = enable; }
int get_syscall_trace(void) { return syscall_trace; }

/* 系统调用名称表（调试用） */
static const char *syscall_names[] = {
    [0]           = "unknown",
    [SYS_fork]    = "fork",    [SYS_exit]   = "exit",
    [SYS_wait]    = "wait",    [SYS_kill]   = "kill",
    [SYS_getpid]  = "getpid",  [SYS_sbrk]   = "sbrk",
    [SYS_read]    = "read",    [SYS_write]  = "write",
    [SYS_open]    = "open",    [SYS_close]  = "close",
    [SYS_sleep]   = "sleep",   [SYS_uptime] = "uptime",
    [SYS_mmap]    = "mmap",    [SYS_munmap] = "munmap",
    [SYS_getpid_fast] = "getpid_fast", [SYS_batch] = "batch",
};

/* ============== 系统调用函数表 ============== */

static int (*syscalls[])(void) = {
    [SYS_fork]   = sys_fork,   [SYS_exit]   = sys_exit,
    [SYS_wait]   = sys_wait,   [SYS_kill]   = sys_kill,
    [SYS_getpid] = sys_getpid, [SYS_sbrk]   = sys_sbrk,
    [SYS_read]   = sys_read,   [SYS_write]  = sys_write,
    [SYS_open]   = sys_open,   [SYS_close]  = sys_close,
    [SYS_sleep]  = sys_sleep,  [SYS_uptime] = sys_uptime,
    [SYS_mmap]   = 0,          [SYS_munmap] = sys_munmap,  /* mmap 特殊处理 */
    [SYS_getpid_fast] = sys_getpid_fast, [SYS_batch] = sys_batch,
};

/* ============== 参数提取 ============== */

/* 从 trapframe 获取第 n 个参数的原始值 */
static uint64 argraw(int n) {
    struct proc *p = myproc();
    if (!p || !p->trapframe) return 0;
    switch (n) {
    case 0: return p->trapframe->a0;
    case 1: return p->trapframe->a1;
    case 2: return p->trapframe->a2;
    case 3: return p->trapframe->a3;
    case 4: return p->trapframe->a4;
    case 5: return p->trapframe->a5;
    default: return 0;
    }
}

/* 获取第 n 个整数参数 */
int argint(int n, int *ip) {
    if (n < 0 || n > 5 || !ip) return -1;
    struct proc *p = myproc();
    if (!p || !p->trapframe) return -1;
    *ip = (int)argraw(n);
    return 0;
}

/* 获取第 n 个地址参数 */
int argaddr(int n, uint64 *ip) {
    if (n < 0 || n > 5 || !ip) return -1;
    struct proc *p = myproc();
    if (!p || !p->trapframe) return -1;
    *ip = argraw(n);
    return 0;
}

/* 获取第 n 个字符串参数，拷贝到内核缓冲区 */
int argstr(int n, char *buf, int max) {
    uint64 addr;
    if (argaddr(n, &addr) < 0) return -1;
    return copyinstr(buf, addr, max);
}

/* ============== 用户内存访问 ============== */

/*
 * 检查用户指针有效性
 * 简化实现：仅检查空指针和溢出
 * TODO: 完整实现应检查页表权限
 */
int check_user_ptr(const void *ptr, int size) {
    uint64 addr = (uint64)ptr;
    if (addr == 0) return -1;                      /* 空指针 */
    if (size > 0 && addr + size < addr) return -1; /* 溢出 */
    return 0;
}

/* 从用户空间拷贝到内核（简化：直接拷贝） */
int copyin(uint64 dst, uint64 src, uint64 len) {
    if (check_user_ptr((void*)src, len) < 0) return -1;
    char *d = (char*)dst, *s = (char*)src;
    while (len--) *d++ = *s++;
    return 0;
}

/* 从内核拷贝到用户空间 */
int copyout(uint64 dst, uint64 src, uint64 len) {
    if (check_user_ptr((void*)dst, len) < 0) return -1;
    char *d = (char*)dst, *s = (char*)src;
    while (len--) *d++ = *s++;
    return 0;
}

/* 从用户空间拷贝字符串，返回长度（不含 '\0'） */
int copyinstr(char *dst, uint64 srcva, uint64 max) {
    if (check_user_ptr((void*)srcva, 1) < 0) return -1;
    char *s = (char*)srcva;
    uint64 i;
    for (i = 0; i < max - 1 && s[i]; i++)
        dst[i] = s[i];
    dst[i] = '\0';
    return i;
}

/* ============== 系统调用分发 ============== */

void syscall(void) {
    struct proc *p = myproc();
    if (!p || !p->trapframe) {
        printf("syscall: no process or trapframe\n");
        return;
    }
    
    int num = p->trapframe->a7;  /* 系统调用号在 a7 */
    const char *name = (num > 0 && num < NSYSCALL) ? syscall_names[num] : "unknown";
    
    if (syscall_trace)
        printf("[syscall] PID %d: %s (num=%d)\n", p->pid, name, num);
    
    if (num > 0 && num < NSYSCALL) {
        if (num == SYS_mmap) {
            /* mmap 返回 uint64，特殊处理 */
            p->trapframe->a0 = sys_mmap();
        } else if (syscalls[num]) {
            p->trapframe->a0 = syscalls[num]();
        } else {
            printf("PID %d: unknown syscall %d\n", p->pid, num);
            p->trapframe->a0 = -1;
        }
    } else {
        printf("PID %d: unknown syscall %d\n", p->pid, num);
        p->trapframe->a0 = -1;
    }
    
    if (syscall_trace)
        printf("[syscall] PID %d: %s returned %ld\n", p->pid, name, (long)p->trapframe->a0);
}

/* ============== 初始化 ============== */

void syscall_init(void) {
    syscall_trace = 0;
}
