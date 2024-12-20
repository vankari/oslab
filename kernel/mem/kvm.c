// kernel virtual memory management

#include "mem/pmem.h"
#include "mem/vmem.h"
#include "lib/print.h"
#include "lib/str.h"
#include "riscv.h"
#include "memlayout.h"

extern char trampoline[]; // in trampoline.S

static pgtbl_t kernel_pgtbl; // 内核页表


// 根据pagetable,找到va对应的pte
// 若设置alloc=true 则在PTE无效时尝试申请一个物理页
// 成功返回PTE, 失败返回NULL
// 提示：使用 VA_TO_VPN PTE_TO_PA PA_TO_PTE
pte_t* vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    assert(va < VA_MAX, "kvm.c->vm_getpte\n");
    for(int level = 2; level > 0; level--) {

        // 在当前页表下,找到va对应的pte
        pte_t* pte = &pgtbl[VA_TO_VPN(va, level)]; 
        
        if(*pte & PTE_V) {   // 有效PTE

            // 更新pagetable指向下一级页表
            pgtbl = (pgtbl_t)PTE_TO_PA(*pte);
        
        } else if(alloc) {  // 无效PTE但是尝试申请
        
            // 申请一个物理页作为页表并清空
            pgtbl = (pgtbl_t)pmem_alloc(true);
            if(pgtbl == NULL) return NULL;
            memset(pgtbl, 0, PGSIZE);

            // 修改PTE中的物理地址并设为有效
            *pte = PA_TO_PTE(pgtbl) | PTE_V;
        
        } else {           // 无效且不尝试申请
        
            return NULL;
        
        }
    }

    return &pgtbl[VA_TO_VPN(va, 0)];
   
}

// 在pgtbl中建立 [va, va + len) -> [pa, pa + len) 的映射
// 本质是找到va在页表对应位置的pte并修改它
// 检查: va pa 应当是 page-aligned, len(字节数) > 0, va + len <= VA_MAX
// 注意: perm 应该如何使用
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    assert(pa % PGSIZE == 0,"veme.c->vm_mappages\n");

    // 确定映射范围
    uint64 first_page = ALIGN_DOWN(va, PGSIZE);
    uint64 last_page  = ALIGN_DOWN(va+len-1, PGSIZE);
    uint64 cur_page   = first_page;
    
    pte_t* pte;
    int count = 0;
    // 逐页映射
    
    while(cur_page <= last_page) {
        
        // 拿到pte并修改它
        pte = vm_getpte(pgtbl, cur_page, true);
        if(pte == NULL) panic("pte NULL");
        *pte = PA_TO_PTE(pa) | perm | PTE_V;

        // 迭代
        cur_page += PGSIZE;
        pa       += PGSIZE;
        count++; 
    }

}

// 解除pgtbl中[va, va+len)区域的映射
// 如果freeit == true则释放对应物理页, 默认是用户的物理页
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    assert(va % PGSIZE == 0, "uvm_unmappages 1\n");
    pte_t* pte;

    // printf("va = %p npages = %d\n", va, npages);
    for(uint64 cur_va = va; cur_va < va + len; cur_va += PGSIZE) {
        
        // 获取 pte 并验证 非空 + 有效 + 指向data page
        pte = vm_getpte(pgtbl, cur_va, false);
        // printf("va = %p, pte = %p\n", cur_va, *pte);
        assert(pte != NULL, "uvm_unmappages 2\n");
        assert((*pte) & PTE_V, "uvm_unmappages 3\n");
        assert((*pte) & (PTE_R | PTE_W | PTE_X) , "uvm_unmappages 4\n");
        
        // 释放占用的物理页,默认用户页
        if(freeit) pmem_free((uint64)PTE_TO_PA(*pte), false);
        
        // 清空pte
        *pte = 0; 
    }
}

// 填充kernel_pgtbl
// 完成 UART CLINT PLIC 内核代码区 内核数据区 可分配区域 trampoline kstack 的映射
void kvm_init()
{
    // 申请L2内核页表空间
    kernel_pgtbl = (pgtbl_t)pmem_alloc(true);
    // printf("kernel pagetable = %p\n",kernel_pagetable);
    assert(kernel_pgtbl != NULL, "kvm.c->kvm_init: 1\n");
    memset(kernel_pgtbl, 0, PGSIZE);


    // uart寄存器映射
    vm_mappages(kernel_pgtbl, UART_BASE, UART_BASE, PGSIZE, PTE_R | PTE_W);
    // PLIC映射
    vm_mappages(kernel_pgtbl, PLIC_BASE, PLIC_BASE, 0x4000, PTE_R | PTE_W);
    vm_mappages(kernel_pgtbl, PLIC_BASE +  0x200000, PLIC_BASE +  0x200000, 0x4000, PTE_R | PTE_W);    
    // CLINT映射
    vm_mappages(kernel_pgtbl, CLINT_BASE, CLINT_BASE, PGSIZE, PTE_R | PTE_W);

    // kernel代码映射
    vm_mappages(kernel_pgtbl, KERNEL_BASE, KERNEL_BASE, (uint64)KERNEL_DATA-KERNEL_BASE, PTE_R | PTE_X );
    //kernel 数据映射
    vm_mappages(kernel_pgtbl,(uint64)KERNEL_DATA,(uint64)KERNEL_DATA,(uint64)ALLOC_BEGIN-(uint64)KERNEL_DATA, PTE_R | PTE_W);
    //可分配区域映射
    vm_mappages(kernel_pgtbl,(uint64)ALLOC_BEGIN,(uint64)ALLOC_BEGIN,(uint64)ALLOC_END-(uint64)ALLOC_BEGIN, PTE_R | PTE_X | PTE_W);
}

// 使用新的页表，刷新TLB
void kvm_inithart()
{
    uint64 addr = MAKE_SATP(kernel_pgtbl);
    w_satp(addr);
    sfence_vma();
}

// for debug
// 输出页表内容
void vm_print(pgtbl_t pgtbl)
{
    // 顶级页表，次级页表，低级页表
    pgtbl_t pgtbl_2 = pgtbl, pgtbl_1 = NULL, pgtbl_0 = NULL;
    pte_t pte;

    printf("level-2 pgtbl: pa = %p\n", pgtbl_2);
    for(int i = 0; i < PGSIZE / sizeof(pte_t); i++) 
    {
        pte = pgtbl_2[i];
        if(!((pte) & PTE_V)) continue;
        assert(PTE_CHECK(pte), "vm_print: pte check fail (1)");
        pgtbl_1 = (pgtbl_t)PTE_TO_PA(pte);
        printf(".. level-1 pgtbl %d: pa = %p\n", i, pgtbl_1);
        
        for(int j = 0; j < PGSIZE / sizeof(pte_t); j++)
        {
            pte = pgtbl_1[j];
            if(!((pte) & PTE_V)) continue;
            assert(PTE_CHECK(pte), "vm_print: pte check fail (2)");
            pgtbl_0 = (pgtbl_t)PTE_TO_PA(pte);
            printf(".. .. level-0 pgtbl %d: pa = %p\n", j, pgtbl_2);

            for(int k = 0; k < PGSIZE / sizeof(pte_t); k++) 
            {
                pte = pgtbl_0[k];
                if(!((pte) & PTE_V)) continue;
                assert(!PTE_CHECK(pte), "vm_print: pte check fail (3)");
                printf(".. .. .. physical page %d: pa = %p flags = %d\n", k, (uint64)PTE_TO_PA(pte), (int)PTE_FLAGS(pte));                
            }
        }
    }
}