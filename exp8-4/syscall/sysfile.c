/*
 * sysfile.c - 文件系统调用
 *
 * 简化实现：仅支持控制台 I/O
 * fd: 0=stdin, 1=stdout, 2=stderr
 */

#include "../include/syscall.h"
#include "../include/proc.h"
#include "../include/printf.h"
#include "../include/uart.h"

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2
#define NOFILE    16

/* sys_read - 从文件读取 */
int sys_read(void) {
    int fd, count;
    uint64 buf;
    
    if (argint(0, &fd) < 0 || argaddr(1, &buf) < 0 || argint(2, &count) < 0)
        return -1;
    if (fd < 0 || fd >= NOFILE || count < 0)
        return -1;
    if (count == 0)
        return 0;
    if (check_user_ptr((void*)buf, count) < 0)
        return -1;
    
    /* 仅支持 stdin */
    if (fd == FD_STDIN) {
        char *p = (char*)buf;
        int i;
        for (i = 0; i < count; i++) {
            int c = uart_getc();
            if (c < 0) break;
            p[i] = (char)c;
            uart_putc(c);  /* 回显 */
            if (c == '\n' || c == '\r') { i++; break; }
        }
        return i;
    }
    return -1;
}

/* sys_write - 向文件写入 */
int sys_write(void) {
    struct proc *p = myproc();
    if (!p || !p->trapframe)
        return -1;
    
    /* 直接从 trapframe 提取参数 */
    int fd = (int)p->trapframe->a0;
    uint64 buf = p->trapframe->a1;
    int count = (int)p->trapframe->a2;
    
    if (fd < 0 || fd >= NOFILE || count < 0)
        return -1;
    if (count == 0)
        return 0;
    if (check_user_ptr((void*)buf, count) < 0)
        return -1;
    
    /* stdout/stderr 输出到控制台 */
    if (fd == FD_STDOUT || fd == FD_STDERR) {
        char *s = (char*)buf;
        for (int i = 0; i < count; i++)
            uart_putc(s[i]);
        return count;
    }
    return -1;
}

/* sys_open - 打开文件（简化：仅支持 /dev/console） */
int sys_open(void) {
    char path[128];
    int flags;
    
    if (argstr(0, path, sizeof(path)) < 0 || argint(1, &flags) < 0)
        return -1;
    
    /* 检查是否为 /dev/console */
    const char *console = "/dev/console";
    int i;
    for (i = 0; console[i] && path[i] == console[i]; i++);
    
    if (console[i] == '\0' && path[i] == '\0')
        return FD_STDOUT;
    
    printf("sys_open: unsupported '%s'\n", path);
    return -1;
}

/* sys_close - 关闭文件 */
int sys_close(void) {
    int fd;
    if (argint(0, &fd) < 0)
        return -1;
    if (fd < 0 || fd >= NOFILE)
        return -1;
    /* 标准 fd 不能关闭，假装成功 */
    if (fd <= FD_STDERR)
        return 0;
    return -1;
}
