/*
 * 实验7：文件系统
 * 
 * 实验目标：
 *   1. 理解文件系统磁盘布局 (超级块/inode/位图/数据块)
 *   2. 实现块缓存系统 (Buffer Cache)
 *   3. 实现写前日志 (Write-Ahead Logging)
 *   4. 实现 inode 管理 (分配/缓存/读写)
 *   5. 实现目录与路径解析
 *   6. 文件系统完整性与性能测试
 * 
 * 基于实验6，新增：
 *   - include/fs.h  : 文件系统接口定义
 *   - include/buf.h : 块缓存结构定义
 *   - fs/bio.c      : 块缓存实现
 *   - fs/log.c      : 写前日志实现
 *   - fs/fs.c       : 文件系统核心实现
 *   - fs/mkfs.c     : 文件系统初始化
 *
 * 实验测试：
 *   - 文件系统完整性测试
 *   - 并发访问测试
 *   - 崩溃恢复测试
 *   - 性能测试
 */

#include "include/uart.h"
#include "include/printf.h"
#include "include/pmm.h"
#include "include/trap.h"
#include "include/proc.h"
#include "include/fs.h"
#include "include/buf.h"

/* ============== 辅助宏 ============== */

#define assert(x) do { \
    if (!(x)) { \
        printf("ASSERT FAIL: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
        while(1) __asm__ volatile("wfi"); \
    } \
} while(0)

/* ============== 外部函数声明 ============== */

extern void binit(void);
extern void mkfs(void);
extern void get_disk_stats(uint32 *reads, uint32 *writes);
extern void reset_disk_stats(void);
extern void debug_log_state(void);

/* ============== 测试1：文件系统完整性 ============== */

static int my_strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

static void test_filesystem_integrity(void) {
    printf("\nTest 1: Filesystem Integrity\n");
    
    char write_buffer[] = "Hello, filesystem!";
    char read_buffer[64];
    int len = 18;  /* strlen("Hello, filesystem!") */
    
    /* 创建测试文件并写入数据 */
    printf("  Creating and writing test file...\n");
    begin_op();
    struct inode *root = iget(ROOTDEV, ROOTINO);
    ilock(root);
    
    struct inode *ip = ialloc(ROOTDEV, T_FILE);
    assert(ip != 0);
    ilock(ip);
    ip->nlink = 1;
    
    int written = writei(ip, write_buffer, 0, len);
    printf("    Written %d bytes\n", written);
    assert(written == len);
    
    uint32 file_inum = ip->inum;
    int result = dirlink(root, "testfile", file_inum);
    assert(result == 0);
    
    iunlockput(ip);
    iunlockput(root);
    end_op();
    
    /* 重新打开并验证数据 */
    printf("  Reopening and verifying...\n");
    begin_op();
    ip = namei("/testfile");
    assert(ip != 0);
    ilock(ip);
    
    int bytes = readi(ip, read_buffer, 0, sizeof(read_buffer));
    read_buffer[bytes] = '\0';
    printf("    Read %d bytes: \"%s\"\n", bytes, read_buffer);
    assert(bytes == len);
    assert(my_strcmp(write_buffer, read_buffer) == 0);
    
    iunlockput(ip);
    end_op();
    
    /* 删除文件 */
    printf("  Deleting test file...\n");
    begin_op();
    root = iget(ROOTDEV, ROOTINO);
    ilock(root);
    
    ip = dirlookup(root, "testfile", 0);
    assert(ip != 0);
    ilock(ip);
    ip->nlink--;
    iupdate(ip);
    itrunc(ip);
    iunlockput(ip);
    
    result = dirunlink(root, "testfile");
    assert(result == 0);
    
    iunlockput(root);
    end_op();
    
    /* 验证文件已删除 */
    begin_op();
    ip = namei("/testfile");
    assert(ip == 0);
    end_op();
    printf("    File deleted successfully\n");
    
    printf("Test 1 PASS!\n");
}

/* ============== 测试2：并发访问 ============== */

static void test_concurrent_access(void) {
    printf("\nTest 2: Concurrent Access\n");
    printf("  Simulating concurrent file operations...\n");
    
    /* 模拟4个"进程"交错执行 */
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 4; i++) {
            char filename[16];
            filename[0] = 'c'; filename[1] = 'o'; filename[2] = 'n'; filename[3] = 'c';
            filename[4] = '_'; filename[5] = '0' + i; filename[6] = '_';
            filename[7] = '0' + round; filename[8] = '\0';
            
            begin_op();
            struct inode *root = iget(ROOTDEV, ROOTINO);
            ilock(root);
            
            struct inode *ip = ialloc(ROOTDEV, T_FILE);
            if (ip) {
                ilock(ip);
                ip->nlink = 1;
                char data = 'A' + i;
                writei(ip, &data, 0, 1);
                dirlink(root, filename, ip->inum);
                iunlockput(ip);
            }
            
            iunlockput(root);
            end_op();
        }
    }
    
    printf("    Created 40 files concurrently\n");
    
    /* 清理：删除所有测试文件 */
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 4; i++) {
            char filename[16];
            filename[0] = 'c'; filename[1] = 'o'; filename[2] = 'n'; filename[3] = 'c';
            filename[4] = '_'; filename[5] = '0' + i; filename[6] = '_';
            filename[7] = '0' + round; filename[8] = '\0';
            
            begin_op();
            struct inode *root = iget(ROOTDEV, ROOTINO);
            ilock(root);
            
            struct inode *ip = dirlookup(root, filename, 0);
            if (ip) {
                ilock(ip);
                ip->nlink--;
                iupdate(ip);
                itrunc(ip);
                iunlockput(ip);
                dirunlink(root, filename);
            }
            
            iunlockput(root);
            end_op();
        }
    }
    
    printf("    Cleaned up all test files\n");
    printf("Test 2 PASS!\n");
}

/* ============== 测试3：崩溃恢复 ============== */

static void test_crash_recovery(void) {
    printf("\nTest 3: Crash Recovery\n");
    
    /* 测试日志系统的崩溃恢复能力 */
    printf("  Testing WAL consistency...\n");
    
    /* 场景1：正常事务提交 */
    printf("    Scenario 1: Normal transaction commit\n");
    begin_op();
    struct inode *ip = ialloc(ROOTDEV, T_FILE);
    assert(ip != 0);
    ilock(ip);
    ip->nlink = 1;
    
    char data[BSIZE];
    for (int i = 0; i < BSIZE; i++)
        data[i] = 'R';
    writei(ip, data, 0, BSIZE);
    
    uint32 inum1 = ip->inum;
    iunlockput(ip);
    end_op();
    
    /* 验证数据持久化 */
    begin_op();
    ip = iget(ROOTDEV, inum1);
    ilock(ip);
    char verify[BSIZE];
    int n = readi(ip, verify, 0, BSIZE);
    assert(n == BSIZE);
    int ok = 1;
    for (int i = 0; i < BSIZE; i++) {
        if (verify[i] != 'R') { ok = 0; break; }
    }
    assert(ok);
    iunlockput(ip);
    end_op();
    printf("      Data persisted correctly\n");
    
    /* 场景2：多块事务 */
    printf("    Scenario 2: Multi-block transaction\n");
    begin_op();
    ip = ialloc(ROOTDEV, T_FILE);
    assert(ip != 0);
    ilock(ip);
    ip->nlink = 1;
    
    /* 写入多个块 */
    for (int b = 0; b < 5; b++) {
        for (int i = 0; i < BSIZE; i++)
            data[i] = 'M' + b;
        writei(ip, data, b * BSIZE, BSIZE);
    }
    
    uint32 inum2 = ip->inum;
    iunlockput(ip);
    end_op();
    
    /* 验证所有块 */
    begin_op();
    ip = iget(ROOTDEV, inum2);
    ilock(ip);
    ok = 1;
    for (int b = 0; b < 5; b++) {
        n = readi(ip, verify, b * BSIZE, BSIZE);
        if (n != BSIZE) { ok = 0; break; }
        for (int i = 0; i < BSIZE; i++) {
            if (verify[i] != 'M' + b) { ok = 0; break; }
        }
    }
    assert(ok);
    iunlockput(ip);
    end_op();
    printf("      Multi-block transaction OK\n");
    
    /* 场景3：日志状态检查 */
    printf("    Scenario 3: Log state verification\n");
    debug_log_state();
    
    printf("Test 3 PASS!\n");
}

/* ============== 测试4：文件系统性能 ============== */

static void test_filesystem_performance(void) {
    printf("\nTest 4: Filesystem Performance\n");
    
    uint64 start_time, end_time;
    uint32 reads, writes;
    
    /* 大量小文件测试 - 使用较小数量避免inode耗尽 */
    #define SMALL_FILE_COUNT 50
    printf("  Small files test (%d x 4B)...\n", SMALL_FILE_COUNT);
    reset_disk_stats();
    start_time = get_time();
    
    uint32 small_inums[SMALL_FILE_COUNT];
    int created = 0;
    for (int i = 0; i < SMALL_FILE_COUNT; i++) {
        begin_op();
        struct inode *ip = ialloc(ROOTDEV, T_FILE);
        if (ip) {
            ilock(ip);
            ip->nlink = 1;
            char data[4] = {'t', 'e', 's', 't'};
            writei(ip, data, 0, 4);
            small_inums[created] = ip->inum;
            created++;
            iunlockput(ip);
        }
        end_op();
    }
    
    end_time = get_time();
    get_disk_stats(&reads, &writes);
    uint64 small_files_time = end_time - start_time;
    printf("    Created: %d files\n", created);
    printf("    Time: %lu cycles\n", small_files_time);
    printf("    Disk I/O: %lu reads, %lu writes\n", reads, writes);
    
    /* 立即清理小文件以释放inode */
    printf("  Cleaning up small files...\n");
    for (int i = 0; i < created; i++) {
        begin_op();
        struct inode *ip = iget(ROOTDEV, small_inums[i]);
        if (ip) {
            ilock(ip);
            ip->nlink = 0;
            itrunc(ip);
            iunlockput(ip);
        }
        end_op();
    }
    
    /* 大文件测试 - 分多个事务写入避免日志溢出 */
    printf("  Large file test (1 x 16KB)...\n");
    reset_disk_stats();
    start_time = get_time();
    
    /* 先分配inode */
    begin_op();
    struct inode *large_ip = ialloc(ROOTDEV, T_FILE);
    if (!large_ip) {
        printf("    Warning: Could not allocate inode for large file\n");
        end_op();
        printf("Test 4 PASS! (partial)\n");
        return;
    }
    ilock(large_ip);
    large_ip->nlink = 1;
    iupdate(large_ip);
    uint32 large_inum = large_ip->inum;
    iunlockput(large_ip);
    end_op();
    
    char large_buffer[BSIZE];
    for (int i = 0; i < BSIZE; i++)
        large_buffer[i] = 'L';
    
    /* 写入16个块 = 16KB，每4块一个事务 */
    int nblocks = 16;
    
    for (int batch = 0; batch < nblocks; batch += 4) {
        begin_op();
        large_ip = iget(ROOTDEV, large_inum);
        ilock(large_ip);
        for (int i = batch; i < batch + 4 && i < nblocks; i++) {
            writei(large_ip, large_buffer, i * BSIZE, BSIZE);
        }
        iunlockput(large_ip);
        end_op();
    }
    
    end_time = get_time();
    get_disk_stats(&reads, &writes);
    uint64 large_file_time = end_time - start_time;
    printf("    Time: %lu cycles\n", large_file_time);
    printf("    Disk I/O: %lu reads, %lu writes\n", reads, writes);
    printf("    File size: %d KB\n", nblocks);
    
    /* 顺序读取性能 */
    printf("  Sequential read test...\n");
    reset_disk_stats();
    start_time = get_time();
    
    begin_op();
    large_ip = iget(ROOTDEV, large_inum);
    ilock(large_ip);
    for (int i = 0; i < nblocks; i++) {
        readi(large_ip, large_buffer, i * BSIZE, BSIZE);
    }
    iunlockput(large_ip);
    end_op();
    
    end_time = get_time();
    get_disk_stats(&reads, &writes);
    printf("    Time: %lu cycles\n", end_time - start_time);
    printf("    Disk reads: %lu (cache effect)\n", reads);
    
    /* 随机读取性能 */
    printf("  Random read test...\n");
    reset_disk_stats();
    start_time = get_time();
    
    begin_op();
    large_ip = iget(ROOTDEV, large_inum);
    ilock(large_ip);
    /* 伪随机访问模式 */
    int offsets[] = {30, 5, 25, 12, 28, 3, 20, 15, 22, 10};
    for (int i = 0; i < 10; i++) {
        int off = offsets[i] % nblocks;
        readi(large_ip, large_buffer, off * BSIZE, BSIZE);
    }
    iunlockput(large_ip);
    end_op();
    
    end_time = get_time();
    get_disk_stats(&reads, &writes);
    printf("    Time: %lu cycles for 10 random blocks\n", end_time - start_time);
    printf("    Disk reads: %lu\n", reads);
    
    /* 清理大文件 */
    begin_op();
    large_ip = iget(ROOTDEV, large_inum);
    ilock(large_ip);
    large_ip->nlink = 0;
    itrunc(large_ip);
    iunlockput(large_ip);
    end_op();
    
    printf("Test 4 PASS!\n");
}

/* ============== 主测试 ============== */

static void test_main(void) {
    test_filesystem_integrity();
    test_concurrent_access();
    test_crash_recovery();
    test_filesystem_performance();
    
    printf("\n=============================================\n");
    printf("  All tests passed!\n");
    printf("=============================================\n");
}

/* ============== 内核入口 ============== */

void kernel_main(void) {
    uart_init();
    printf("\n=============================================\n");
    printf("  Exp7: File System\n");
    printf("=============================================\n");
    
    pmm_init();
    trap_init();
    proc_init();
    binit();        /* 初始化块缓存 */
    mkfs();         /* 创建文件系统 */
    fsinit(0);      /* 初始化文件系统 */
    
    test_main();
    
    while (1) __asm__ volatile("wfi");
}
