#include "lib/print.h"
#include "lib/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "memlayout.h"

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();


// 第一个进程
static proc_t proczero;

// 获得一个初始化过的用户页表
// 完成了trapframe 和 trampoline 的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    pgtbl_t ret=(pgtbl_t)pmem_alloc(false);
    vm_mappages(ret,TRAPFRAME,trapframe,PGSIZE,PTE_R|PTE_W);
    vm_mappages(ret,TRAMPOLINE,(uint64)trampoline,PGSIZE,PTE_R|PTE_X);
    return ret;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问
*/
void proc_make_fisrt()
{
    uint64 page;
    
    // pid 设置
    proczero.pid=0;
    // pagetable 初始化
    page=(uint64)pmem_alloc(false);
    proczero.pgtbl=proc_pgtbl_init(page);
    uint64 tf = page;
    // ustack 映射 + 设置 ustack_pages 
    page=(uint64)pmem_alloc(false);
    vm_mappages(proczero.pgtbl,TRAPFRAME-PGSIZE,page,PGSIZE,PTE_R|PTE_W|PTE_U);
    proczero.ustack_pages=1;
    // data + code 映射
    assert(initcode_len <= PGSIZE, "proc_make_first: initcode too big\n");
    page=(uint64)pmem_alloc(false);
    memset((void*)page,0,PGSIZE);
    vm_mappages(proczero.pgtbl,PGSIZE,page,PGSIZE,PTE_R|PTE_X|PTE_W|PTE_U);
    memmove((void*)page,(void*)initcode,initcode_len);
    //和xv6 book的heap和stack方向相反
    // 设置 heap_top
    proczero.heap_top=2*PGSIZE;
    // tf字段设置
    proczero.tf =(trapframe_t*)tf;
    proczero.tf->epc = PGSIZE;
    proczero.tf->sp = TRAPFRAME;
    // 内核字段设置
    proczero.kstack=KSTACK(0);
    // 上下文切换
    memset(&proczero.ctx,0,sizeof(proczero.ctx));
    proczero.ctx.sp=proczero.kstack+PGSIZE;
    proczero.ctx.ra=(uint64)trap_user_return;
    cpu_t* cur = mycpu();
    cur->proc=&proczero;
    swtch(&(cur->ctx),&proczero.ctx);
}