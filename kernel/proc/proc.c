#include "lib/print.h"
#include "lib/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/mmap.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "memlayout.h"
#include "riscv.h"

/*----------------外部空间------------------*/

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();
extern pgtbl_t kernel_pgtbl;

/*----------------本地变量------------------*/

// 进程数组
static proc_t procs[NPROC];

// 第一个进程的指针
static proc_t* proczero;

// 全局的pid和保护它的锁 
static int global_pid = 1;
static spinlock_t lk_pid;


// 申请一个pid(锁保护)
static int alloc_pid()
{
    int tmp = 0;
    spinlock_acquire(&lk_pid);
    assert(global_pid >= 0, "alloc_pid: overflow");
    tmp = global_pid++;
    spinlock_release(&lk_pid);
    return tmp;
}

// 释放锁 + 调用 trap_user_return
static void fork_return()
{
    // 由于调度器中上了锁，所以这里需要解锁
    proc_t* p = myproc();
    spinlock_release(&p->lk);
    trap_user_return();
}

// 返回一个未使用的进程空间
// 设置pid + 设置上下文中的ra和sp
// 申请tf和pgtbl使用的物理页
proc_t* proc_alloc()
{
    proc_t *p;

    // 遍历进程数组，查找状态为 UNUSED 的进程
    for (p = &procs[0]; p < &procs[NPROC]; p++) {
        spinlock_acquire(&p->lk);

        if (p->state == UNUSED) {
            // 设置进程相关字段
            p->pid = alloc_pid(); // 分配一个新的 PID

            // 分配 trapframe 和页表使用的物理页
            p->tf = pmem_alloc(false);
            if (!p->tf) {
                proc_free(p); // 回收资源
                spinlock_release(&p->lk);
                return NULL; // 分配失败
            }

            p->pgtbl = proc_pgtbl_init((uint64)p->tf);
            if (!p->pgtbl) {
                proc_free(p); // 回收资源
                spinlock_release(&p->lk);
                return NULL; // 分配失败
            }

            // 设置上下文中的 sp 和 ra
            memset(&p->ctx, 0, sizeof(p->ctx));
            p->ctx.ra = (uint64)fork_return;        // 返回地址
            p->ctx.sp = p->kstack + PGSIZE;         // 内核栈指针

            // 返回已分配的进程，锁未释放
            return p;
        }
        spinlock_release(&p->lk);
    }

    return NULL;

}

// 释放一个进程空间
// 释放pgtbl的整个地址空间
// 释放mmap_region到仓库
// 设置其余各个字段为合适初始值
// tips: 调用者需持有p->lk
void proc_free(proc_t* p)
{
    if (p->tf) {
        pmem_free((uint64)p->tf, false);
        p->tf = NULL;
    }
    // 销毁页表
    if (p->pgtbl) {
        uvm_destroy_pgtbl(p->pgtbl); 
        p->pgtbl = NULL;         
    }

    // 释放 mmap_region
    mmap_region_t* current_region = p->mmap;
    while (current_region) {
        mmap_region_t* temp = current_region;
        current_region = current_region->next;
        mmap_region_free(temp); // 释放单个映射区域的内存
    }
    p->mmap = NULL; // 重置 mmap 指针

    // 重置进程字段
    p->pid = -1;
    p->state = UNUSED;
    p->parent = NULL;
    p->exit_state = 0;
    memset(&(p->ctx), 0, sizeof(context_t));
}

// 进程模块初始化
void proc_init()
{
    proc_t *proc;

    // 初始化全局 PID 锁
    spinlock_init(&lk_pid, "global_pid_lock");

    // 遍历所有进程
    for (proc = &procs[0]; proc < &procs[NPROC]; proc++) {
        // 初始化每个进程的自旋锁
        spinlock_init(&proc->lk, "proc_lock");

        // 为进程分配物理页作为内核栈
        void *kstack_page = pmem_alloc(true);
        if (kstack_page == 0) {
            panic("proc_init: pmem_alloc failed.");
        }

        // 获取内核栈的虚拟地址
        uint64 kernel_stack_addr = KSTACK((int)(proc - &procs[0]));

        // 将分配的物理内存映射到内核页表
        vm_mappages(kernel_pgtbl, kernel_stack_addr, (uint64)kstack_page, PGSIZE, PTE_R | PTE_W);

        // 保存内核栈虚拟地址
        proc->kstack = kernel_stack_addr;

        // 初始化进程的其他字段
        spinlock_acquire(&proc->lk);
        proc_free(proc); // 将进程标记为空闲并释放资源
        spinlock_release(&proc->lk);
    }

    // 刷新TLB
    kvm_inithart();
}

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

    UNUSED -> RUNNABLE
*/
void proc_make_first()
{
    uint64 page;
    proczero = proc_alloc();
    // ustack 映射 + 设置 ustack_pages 
    page=(uint64)pmem_alloc(false);
    vm_mappages(proczero->pgtbl,TRAPFRAME-PGSIZE,page,PGSIZE,PTE_R|PTE_W|PTE_U);
    proczero->ustack_pages=1;
    // data + code 映射
    assert(initcode_len <= PGSIZE, "proc_make_first: initcode too big\n");
    page=(uint64)pmem_alloc(false);
    memset((void*)page,0,PGSIZE);
    vm_mappages(proczero->pgtbl,PGSIZE,page,PGSIZE,PTE_R|PTE_X|PTE_W|PTE_U);
    memmove((void*)page,(void*)initcode,initcode_len);
    //和xv6 book的heap和stack方向相反
    // 设置 heap_top
    proczero->heap_top=USER_BASE+PGSIZE;
    // 设置 mmap_region_t
    proczero->mmap = mmap_region_alloc();
    // tf字段设置
    proczero->tf->epc = USER_BASE;
    proczero->tf->sp = TRAPFRAME;
    proczero->state=RUNNABLE;
    spinlock_release(&proczero->lk);
}

// 进程复制
// UNUSED -> RUNNABLE
int proc_fork()
{
    proc_t* parent = myproc(); // 获取当前进程（父进程）
    proc_t* child = proc_alloc();
    if (child == NULL) {
        return -1; // 分配失败，返回错误码
    }

    // 复制父进程的 trapframe
    memmove((void*)child->tf,(void*)parent->tf,PGSIZE);
    child->tf->a0 = 0;

    // 复制父进程的页表
    uvm_copy_pgtbl(parent->pgtbl, child->pgtbl, parent->heap_top, parent->ustack_pages, parent->mmap);

    // 复制 mmap 链表
    if (parent->mmap) {
        child->mmap = mmap_region_alloc();
        if (child->mmap == NULL) {
            // mmap 分配失败，释放资源并返回错误码
            proc_free(child);
            spinlock_release(&child->lk);
            return -1;
        }

        mmap_region_t* src_region = parent->mmap;
        mmap_region_t* dst_region = child->mmap;

        while (src_region) {
            dst_region->begin = src_region->begin;
            dst_region->npages = src_region->npages;

            if (src_region->next) {
                dst_region->next = mmap_region_alloc();
                if (dst_region->next == NULL) {
                    // 如果分配失败，释放子进程的资源
                    proc_free(child);
                    spinlock_release(&child->lk);
                    return -1;
                }
            } else {
                dst_region->next = NULL;
            }

            src_region = src_region->next;
            dst_region = dst_region->next;
        }
    } else {
        child->mmap = NULL;
    }
    //
    // 复制堆顶和用户栈页数
    child->heap_top = parent->heap_top;
    child->ustack_pages = parent->ustack_pages;

    // 设置子进程的父进程
    child->parent = parent;

    // 设置子进程为 RUNNABLE，表示可以被调度
    child->state = RUNNABLE;

    // 释放子进程的锁
    spinlock_release(&child->lk);
    return child->pid;
}

// 进程放弃CPU的控制权
// RUNNING -> RUNNABLE
void proc_yield()
{
    proc_t *proc = myproc();
    spinlock_acquire(&proc->lk);
    proc->state = RUNNABLE;
    proc_sched();
    spinlock_release(&proc->lk);
}

// 等待一个子进程进入 ZOMBIE 状态
// 将退出的子进程的exit_state放入用户给的地址 addr
// 成功返回子进程pid，失败返回-1
int proc_wait(uint64 addr)
{
    proc_t *np;             
    int have_child = 0;     
    int pid = -1;        
    proc_t *parent = myproc(); 

    spinlock_acquire(&parent->lk); 

    while (1) {
        have_child = 0; 

        for (np = procs; np < &procs[NPROC]; np++) {
            if (np->parent == parent) { 
                have_child = 1;

                spinlock_acquire(&np->lk); 

                if (np->state == ZOMBIE) {
                    pid = np->pid;
                    uvm_copyout(parent->pgtbl, addr, (uint64)&np->exit_state, sizeof(np->exit_state));
                    proc_free(np);
                    spinlock_release(&np->lk);
                    spinlock_release(&parent->lk);

                    return pid; 
                }
                spinlock_release(&np->lk);
            }
        }
        if (!have_child) {
            spinlock_release(&parent->lk);
            return -1;
        }

        
        proc_sleep(parent, &parent->lk);
    }
}

// 唤醒一个进程
static void proc_wakeup_one(proc_t* p)
{
    assert(spinlock_holding(&p->lk), "proc_wakeup_one: lock");
    if(p->state == SLEEPING && p->sleep_space == p) {
        p->state = RUNNABLE;
    }
}
// 父进程退出，子进程认proczero做父，因为它永不退出
static void proc_reparent(proc_t* parent)
{
    proc_t *p;
    for(p = &procs[0]; p < &procs[NPROC]; p++)
    {
        if(p->parent == parent)
        {
            p->parent = proczero;
            proc_wakeup_one(proczero);
        }
    }
}

// 进程退出
void proc_exit(int exit_state)
{
    proc_t *p = myproc(); 
    if (p == proczero) {
        panic("proc_exit: proczero can not exit");
    }
    spinlock_acquire(&proczero->lk);
    proc_wakeup_one(proczero);
    spinlock_release(&proczero->lk);
    spinlock_acquire(&p->lk);
    proc_t *parent = p->parent; 
    spinlock_release(&p->lk);
    spinlock_acquire(&p->lk);
    proc_reparent(p);
    spinlock_release(&p->lk);
    spinlock_acquire(&parent->lk);
    proc_wakeup_one(parent);
    spinlock_release(&parent->lk);

    spinlock_acquire(&p->lk);
    p->exit_state = exit_state; // 设置退出状态
    p->state = ZOMBIE;          // 将状态置为 ZOMBIE
    proc_sched();
}

// 进程切换到调度器
// ps: 调用者保证持有当前进程的锁
void proc_sched()
{
    proc_t *p = myproc(); // 获取当前进程指针
    push_off();
    cpu_t* c =mycpu();
    pop_off();
    // 确保调用者持有当前进程的锁
    if (!spinlock_holding(&p->lk)) {
        printf("proc_sched: lock not held by process %d\n", p->pid);
        panic("proc_sched: lock not held.");
    }

    // 确保当前 CPU 
    if (c->noff != 1) {
        printf("proc_sched: CPU noff = %d\n", c->noff);
        panic("proc_sched: CPU noff != 1.");
    }

    // 确保当前进程不是RUNNING状态
    if (p->state == RUNNING) {
        printf("proc_sched: process %d is still running\n", p->pid);
        panic("proc_sched: running process cannot schedule.");
    }

    // 确保设备中断已禁用
    if (intr_get()) {
        panic("proc_sched: device interrupts enabled.");
    }

    int origin = c->origin;
    swtch(&p->ctx, &c->ctx);
    c->origin = origin;
}

// 调度器
void proc_scheduler()
{
    proc_t *p;              // 指向进程表中的某个进程
    push_off();
    cpu_t *c = mycpu();     // 当前 CPU 的信息
    pop_off();
    // 设置当前 CPU 核心的运行进程为空
    c->proc = NULL;

    while(1) {
        // 启用中断，允许设备中断发生
        //intr_on();
        // 遍历进程表，寻找RUNNABLE状态的进程
        for (p = &procs[0]; p < &procs[NPROC]; p++) {
            spinlock_acquire(&p->lk); // 获取进程锁

            if (p->state == RUNNABLE) {
                // 进程状态切换为 `RUNNING`
                printf("proc_scheduler: process %d switching to RUNNING\n", p->pid); // 调试信息
                p->state = RUNNING;
               // if(p->parent!=NULL)printf("p->parent->pid:%d\n",p->parent->pid);
                c->proc = p;
                swtch(&c->ctx, &p->ctx); 
                c->proc = NULL;
            }

            spinlock_release(&p->lk); // 释放进程锁
        }
    }
}

// 进程睡眠在sleep_space
void proc_sleep(void* sleep_space, spinlock_t* lk)
{
    proc_t *p = myproc(); // 获取当前进程
    assert(lk != NULL, "proc_sleep: lock is NULL");
    if (lk != &p->lk) {
        spinlock_acquire(&p->lk); 
        spinlock_release(lk);    
    }

    // 设置进程为睡眠状态
    p->sleep_space = sleep_space; 
    p->state = SLEEPING;        

    proc_sched();

    p->sleep_space = NULL;

    if (lk != &p->lk) {
        spinlock_release(&p->lk); 
        spinlock_acquire(lk);
    }
}

// 唤醒所有在sleep_space沉睡的进程
void proc_wakeup(void* sleep_space)
{
     proc_t *proc;
    for(proc = &procs[0]; proc < &procs[NPROC]; proc++) 
    {
        spinlock_acquire(&proc->lk);
        if(proc->state == SLEEPING && proc->sleep_space == sleep_space)
            proc->state = RUNNABLE;
        spinlock_release(&proc->lk);
    }
}