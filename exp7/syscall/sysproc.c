/*
 * sysproc.c - 进程系统调用实现
 * 
 * 功能：
 *   - 进程控制 (fork/exit/wait/kill/getpid)
 *   - 内存管理 (sbrk/mmap/munmap)
 *   - 时间管理 (sleep/uptime)
 *   - 性能优化 (getpid_fast/batch)
 */

#include "../include/syscall.h"
#include "../include/proc.h"
#include "../include/trap.h"
#include "../include/pmm.h"
#include "../include/printf.h"

/* sys_fork - 创建子进程 */
int sys_fork(void) {
    return fork();
}

/* sys_exit - 终止当前进程 */
int sys_exit(void) {
    int status;
    if (argint(0, &status) < 0) status = 0;
    exit_process(status);
    return 0;  /* 不会到达 */
}

/* sys_wait - 等待子进程 */
int sys_wait(void) {
    uint64 addr;
    if (argaddr(0, &addr) < 0)
        return -1;
    if (addr == 0)
        return wait_process(0);
    if (check_user_ptr((void*)addr, sizeof(int)) < 0)
        return -1;
    return wait_process((int*)addr);
}

/* sys_kill - 终止指定进程 */
int sys_kill(void) {
    int pid;
    if (argint(0, &pid) < 0) return -1;
    return kill(pid);
}

/* sys_getpid - 获取当前 PID */
int sys_getpid(void) {
    struct proc *p = myproc();
    return p ? p->pid : -1;
}

/* sys_sbrk - 调整堆大小（简化实现） */
int sys_sbrk(void) {
    struct proc *p = myproc();
    if (!p || !p->trapframe)
        return -1;
    
    int n = (int)p->trapframe->a0;
    uint64 old_sz = p->sz;
    uint64 new_sz = old_sz + n;
    
    /* 溢出检查 */
    if (n > 0 && new_sz < old_sz) return -1;
    if (n < 0 && new_sz > old_sz) return -1;
    
    p->sz = new_sz;
    return old_sz;
}

/* sys_sleep - 进程睡眠 */
int sys_sleep(void) {
    int n;
    if (argint(0, &n) < 0) return -1;
    if (n <= 0) return 0;
    
    uint64 ticks0 = get_ticks();
    while (get_ticks() - ticks0 < (uint64)n) {
        struct proc *p = myproc();
        if (p && p->killed) return -1;
        yield();
    }
    return 0;
}

/* sys_uptime - 获取系统运行时间 */
int sys_uptime(void) {
    return get_ticks();
}

/* ============== mmap/munmap 实现 ============== */

#define PGROUNDUP(sz)   (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a)  ((a) & ~(PGSIZE-1))

/* 查找空闲 VMA 槽位 */
static struct vma* find_free_vma(struct proc *p) {
    for (int i = 0; i < NVMA; i++) {
        if (!p->vmas[i].used)
            return &p->vmas[i];
    }
    return 0;
}

/* 查找包含指定地址的 VMA */
static struct vma* find_vma(struct proc *p, uint64 addr) {
    for (int i = 0; i < NVMA; i++) {
        struct vma *v = &p->vmas[i];
        if (v->used && addr >= v->addr && addr < v->addr + v->length)
            return v;
    }
    return 0;
}

/*
 * sys_mmap - 内存映射（简化实现）
 * 
 * 参数通过 trapframe 传递：
 *   a0: addr   - 建议的映射地址（0 表示由内核选择）
 *   a1: length - 映射长度
 *   a2: prot   - 保护标志 (PROT_READ/WRITE/EXEC)
 *   a3: flags  - 映射标志 (MAP_SHARED/PRIVATE/ANONYMOUS)
 *   a4: fd     - 文件描述符（匿名映射时为 -1）
 *   a5: offset - 文件偏移
 * 
 * 返回：成功返回映射地址，失败返回 -1
 */
uint64 sys_mmap(void) {
    struct proc *p = myproc();
    if (!p || !p->trapframe)
        return -1;
    
    /* 提取参数 */
    uint64 length = p->trapframe->a1;
    int prot      = (int)p->trapframe->a2;
    int flags     = (int)p->trapframe->a3;
    
    /* 参数验证 */
    if (length == 0)
        return -1;
    
    /* 长度对齐到页边界 */
    length = PGROUNDUP(length);
    
    /* 当前仅支持匿名映射 */
    if (!(flags & MAP_ANONYMOUS))
        return -1;
    
    /* 查找空闲 VMA */
    struct vma *v = find_free_vma(p);
    if (!v)
        return -1;
    
    /* 分配物理内存 */
    uint64 npages = length / PGSIZE;
    char *mem = 0;
    
    if (npages == 1) {
        mem = (char*)alloc_page();  /* alloc_page 已清零 */
    } else {
        mem = (char*)alloc_pages(npages);
        if (mem) {
            /* 手动清零 */
            for (uint64 i = 0; i < length; i++)
                mem[i] = 0;
        }
    }
    
    if (!mem)
        return -1;
    
    /* 设置 VMA */
    v->addr = (uint64)mem;
    v->length = length;
    v->prot = prot;
    v->flags = flags;
    v->fd = -1;
    v->offset = 0;
    v->used = 1;
    
    return (uint64)mem;
}

/*
 * sys_munmap - 解除内存映射
 * 
 * 参数通过 trapframe 传递：
 *   a0: addr   - 映射起始地址
 *   a1: length - 解除映射的长度
 * 
 * 返回：成功返回 0，失败返回 -1
 */
int sys_munmap(void) {
    struct proc *p = myproc();
    if (!p || !p->trapframe)
        return -1;
    
    uint64 addr   = p->trapframe->a0;
    uint64 length = p->trapframe->a1;
    
    /* 参数验证 */
    if (length == 0)
        return -1;
    
    /* 地址必须页对齐 */
    if (addr != PGROUNDDOWN(addr))
        return -1;
    
    length = PGROUNDUP(length);
    
    /* 查找对应的 VMA */
    struct vma *v = find_vma(p, addr);
    if (!v)
        return -1;
    
    /* 简化：只支持完整解除映射 */
    if (addr != v->addr || length != v->length)
        return -1;
    
    /* 释放物理内存 */
    uint64 npages = v->length / PGSIZE;
    if (npages == 1) {
        free_page((void*)v->addr);
    } else {
        free_pages((void*)v->addr, npages);
    }
    
    /* 清除 VMA */
    v->used = 0;
    v->addr = 0;
    v->length = 0;
    v->prot = 0;
    v->flags = 0;
    v->fd = -1;
    v->offset = 0;
    
    return 0;
}

/* ============== 性能优化系统调用 ============== */

/* sys_getpid_fast - 快速 getpid，无额外检查 */
int sys_getpid_fast(void) {
    struct proc *p = myproc();
    return p ? p->pid : -1;
}

/* 批量系统调用请求结构 */
struct batch_request {
    int64 syscall_num;  /* 系统调用号 */
    uint64 args[6];     /* 参数 (a0-a5) */
    int64 result;       /* 返回值 */
};

/* 内部分发函数，用于批量调用 */
static int64 batch_dispatch(struct proc *p, int num) {
    switch (num) {
    case SYS_getpid:      return p->pid;
    case SYS_getpid_fast: return p->pid;
    case SYS_uptime:      return sys_uptime();
    case SYS_sbrk:        return sys_sbrk();
    case SYS_fork:        return sys_fork();
    case SYS_exit:        return sys_exit();
    case SYS_wait:        return sys_wait();
    case SYS_kill:        return sys_kill();
    case SYS_sleep:       return sys_sleep();
    case SYS_mmap:        return (int64)sys_mmap();
    case SYS_munmap:      return sys_munmap();
    default:              return -1;
    }
}

/*
 * sys_batch - 批量系统调用
 *
 * 优化原理：将 N 次用户态/内核态切换减少为 1 次
 * 参数：a0=requests数组指针, a1=count数量
 * 返回：成功执行的系统调用数量
 */
int sys_batch(void) {
    struct proc *p = myproc();
    if (!p || !p->trapframe)
        return -1;
    
    uint64 req_addr = p->trapframe->a0;
    int count = (int)p->trapframe->a1;
    
    if (count <= 0 || count > BATCH_MAX)
        return -1;
    if (check_user_ptr((void*)req_addr, count * sizeof(struct batch_request)) < 0)
        return -1;
    
    struct batch_request *reqs = (struct batch_request*)req_addr;
    int done = 0;
    
    /* 保存原始 trapframe */
    uint64 save[7] = {
        p->trapframe->a0, p->trapframe->a1, p->trapframe->a2,
        p->trapframe->a3, p->trapframe->a4, p->trapframe->a5,
        p->trapframe->a7
    };
    
    /* 执行每个请求 */
    for (int i = 0; i < count; i++) {
        int num = reqs[i].syscall_num;
        
        if (num <= 0 || num >= NSYSCALL || num == SYS_batch) {
            reqs[i].result = -1;
            continue;
        }
        
        /* 设置参数 */
        p->trapframe->a0 = reqs[i].args[0];
        p->trapframe->a1 = reqs[i].args[1];
        p->trapframe->a2 = reqs[i].args[2];
        p->trapframe->a3 = reqs[i].args[3];
        p->trapframe->a4 = reqs[i].args[4];
        p->trapframe->a5 = reqs[i].args[5];
        p->trapframe->a7 = num;
        
        reqs[i].result = batch_dispatch(p, num);
        done++;
    }
    
    /* 恢复 trapframe */
    p->trapframe->a0 = save[0];
    p->trapframe->a1 = save[1];
    p->trapframe->a2 = save[2];
    p->trapframe->a3 = save[3];
    p->trapframe->a4 = save[4];
    p->trapframe->a5 = save[5];
    p->trapframe->a7 = save[6];
    
    return done;
}
