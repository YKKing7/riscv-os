/*
 * 虚拟内存管理实现
 * 
 * Sv39 地址格式：
 *   VA (39位): VPN[2](9) | VPN[1](9) | VPN[0](9) | Offset(12)
 *   PA (56位): PPN[2](26) | PPN[1](9) | PPN[0](9) | Offset(12)
 * 
 * 大页支持：
 *   - 2MB 大页 (Megapage): level 1 叶子节点
 *   - 1GB 大页 (Gigapage): level 2 叶子节点
 */

#include "../include/vm.h"
#include "../include/pmm.h"
#include "../include/memlayout.h"

/* 链接脚本符号 */
extern char _etext[];
extern char _end[];

/* 内核页表 */
pagetable_t kernel_pagetable = 0;

/* 大页大小定义 */
#define MEGAPAGE_SIZE   (PGSIZE * 512)          /* 2MB */
#define GIGAPAGE_SIZE   (MEGAPAGE_SIZE * 512)   /* 1GB */

/* 大页对齐检查 */
#define IS_MEGAPAGE_ALIGNED(x)  (((x) & (MEGAPAGE_SIZE - 1)) == 0)
#define IS_GIGAPAGE_ALIGNED(x)  (((x) & (GIGAPAGE_SIZE - 1)) == 0)

/* ============== 页表遍历 ============== */

/*
 * 遍历页表，查找或创建 PTE
 */
pte_t* walk(pagetable_t pt, uint64 va, int alloc) {
    if (va >= MAXVA)
        return 0;
    
    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pt[VA_VPN(va, level)];
        
        if (*pte & PTE_V) {
            pt = (pagetable_t)PTE2PA(*pte);
        } else {
            if (!alloc)
                return 0;
            pt = (pagetable_t)alloc_page();
            if (!pt)
                return 0;
            *pte = PA2PTE((uint64)pt) | PTE_V;
        }
    }
    return &pt[VA_VPN(va, 0)];
}

pte_t* walk_lookup(pagetable_t pt, uint64 va) { return walk(pt, va, 0); }
pte_t* walk_create(pagetable_t pt, uint64 va) { return walk(pt, va, 1); }

/* ============== 页表创建与销毁 ============== */

pagetable_t create_pagetable(void) {
    return (pagetable_t)alloc_page();  /* alloc_page 已清零 */
}

/* 递归释放页表 */
static void freewalk(pagetable_t pt, int level) {
    for (int i = 0; i < NPTENTRIES; i++) {
        pte_t pte = pt[i];
        if ((pte & PTE_V) && level > 0 && !(pte & (PTE_R | PTE_W | PTE_X))) {
            freewalk((pagetable_t)PTE2PA(pte), level - 1);
        }
    }
    free_page(pt);
}

void destroy_pagetable(pagetable_t pt) {
    if (pt) freewalk(pt, 2);
}

/* ============== 映射管理 ============== */

int map_page(pagetable_t pt, uint64 va, uint64 pa, int perm) {
    if ((va | pa) % PGSIZE != 0)
        return -1;
    
    pte_t *pte = walk_create(pt, va);
    if (!pte)
        return -1;
    
    *pte = PA2PTE(pa) | perm | PTE_V;
    return 0;
}

/*
 * 映射 2MB 大页 (Megapage)
 * va 和 pa 必须 2MB 对齐
 */
int map_megapage(pagetable_t pt, uint64 va, uint64 pa, int perm) {
    if (!IS_MEGAPAGE_ALIGNED(va) || !IS_MEGAPAGE_ALIGNED(pa))
        return -1;
    if (va >= MAXVA)
        return -1;
    
    /* 遍历到 level 1 */
    pte_t *pte = &pt[VA_VPN(va, 2)];
    if (!(*pte & PTE_V)) {
        pagetable_t child = (pagetable_t)alloc_page();
        if (!child) return -1;
        *pte = PA2PTE((uint64)child) | PTE_V;
    }
    
    pagetable_t l1 = (pagetable_t)PTE2PA(*pte);
    pte = &l1[VA_VPN(va, 1)];
    
    /* 设置为大页叶子节点 */
    *pte = PA2PTE(pa) | perm | PTE_V;
    return 0;
}

/*
 * 智能区域映射 - 自动选择大页或普通页
 */
int map_region(pagetable_t pt, uint64 va, uint64 pa, uint64 size, int perm) {
    if (size == 0)
        return -1;
    
    uint64 end = va + size;
    
    while (va < end) {
        uint64 remaining = end - va;
        
        /* 尝试使用 2MB 大页 */
        if (remaining >= MEGAPAGE_SIZE && 
            IS_MEGAPAGE_ALIGNED(va) && IS_MEGAPAGE_ALIGNED(pa)) {
            if (map_megapage(pt, va, pa, perm) != 0)
                return -1;
            va += MEGAPAGE_SIZE;
            pa += MEGAPAGE_SIZE;
            continue;
        }
        
        /* 使用普通 4KB 页 */
        if (map_page(pt, va, pa, perm) != 0)
            return -1;
        va += PGSIZE;
        pa += PGSIZE;
    }
    
    return 0;
}

int unmap_page(pagetable_t pt, uint64 va) {
    pte_t *pte = walk_lookup(pt, va);
    if (!pte || !(*pte & PTE_V))
        return -1;
    *pte = 0;
    return 0;
}

/* ============== 地址转换 ============== */

uint64 va2pa(pagetable_t pt, uint64 va) {
    pte_t *pte = walk_lookup(pt, va);
    if (!pte || !(*pte & PTE_V))
        return 0;
    return PTE2PA(*pte) | VA_OFFSET(va);
}

/* ============== 内核页表管理 ============== */

/*
 * 初始化内核页表 (恒等映射)
 */
void kvminit(void) {
    kernel_pagetable = create_pagetable();
    if (!kernel_pagetable)
        return;
    
    /* 代码段: R+X */
    map_region(kernel_pagetable, KERNBASE, KERNBASE,
               (uint64)_etext - KERNBASE, PTE_R | PTE_X);
    
    /* 数据段 + 堆: R+W */
    map_region(kernel_pagetable, (uint64)_etext, (uint64)_etext,
               PHYSTOP - (uint64)_etext, PTE_R | PTE_W);
    
    /* UART 设备 */
    map_region(kernel_pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);
}

/*
 * 激活内核页表
 */
void kvminithart(void) {
    w_satp(MAKE_SATP(kernel_pagetable));
    sfence_vma();
}

