#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/mmap.h"
#include "lib/str.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "syscall/sysfunc.h"
#include "syscall/syscall.h"

// 堆伸缩
// uint64 new_heap_top 新的堆顶 (如果是0代表查询, 返回旧的堆顶)
// 成功返回新的堆顶 失败返回-1
uint64 sys_brk()
{
    proc_t* p =myproc();
    uint64 tar, cur; 
    arg_uint64(0, &tar);
    cur = p->heap_top;
    uint64 heap_top;
    if(tar==0){
        return cur;
    }//查询
    else if(tar>cur){
        heap_top= uvm_heap_grow(p->pgtbl,cur,tar-cur);
    }
    else if(tar<cur){
        heap_top= uvm_heap_ungrow(p->pgtbl,cur,cur-tar);
    }
    else{
        heap_top=cur;
    }
    if(heap_top!=tar)return -1;
    else{
        p->heap_top=heap_top;
        return heap_top;
    }
}

// 内存映射
// uint64 start 起始地址 (如果为0则由内核自主选择一个合适的起点)
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回映射空间的起始地址, 失败返回-1
uint64 sys_mmap()
{
    uint64 start;
    uint32 len,perm;

    arg_uint64(0, &start);
    arg_uint32(1, &len);
    arg_uint32(2, &perm);
    if(start==0&&len%PGSIZE==0){
        proc_t* p=myproc();
        mmap_region_t* s= p->mmap;
        while(s!=NULL){
            if(s->npages>=len/PGSIZE){
                start=s->begin;
                uvm_mmap(start,len/PGSIZE,perm);
                break;
            }
            s=s->next;
        }
        if(s==NULL) return -1;
    }
    else if(start%PGSIZE==0&&len%PGSIZE==0){
        uvm_mmap(start, len/PGSIZE, perm);
    }
    else{
        return -1;
    }
    return start;

}

// 取消内存映射
// uint64 start 起始地址
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回0 失败返回-1
uint64 sys_munmap()
{
    uint64 start;
    uint32 len;

    arg_uint64(0, &start);
    arg_uint32(1, &len);
    if(start%PGSIZE==0&&len%PGSIZE==0){
        uvm_munmap(start,len/PGSIZE);

        return 0;
    }
    else{
        return -1;
    }

}

// 打印字符
// uint64 addr
uint64 sys_print()
{
    char str[128] = {0};
    arg_str(0, str, 128);
    printf("%s", str, 128);
    return 0;
}

// 进程复制
uint64 sys_fork()
{
    int pid = proc_fork();
    if (pid < 0) {
        return -1;
    }
    return pid;
}

// 进程等待
// uint64 addr  子进程退出时的exit_state需要放到这里 
uint64 sys_wait()
{
    uint64 addr;
    arg_uint64(0, &addr);
    int pid = proc_wait(addr); 
    return pid;
}

// 进程退出
// int exit_state
uint64 sys_exit()
{
    uint64 exit_state;
    arg_uint64(0, &exit_state);
    proc_exit(exit_state); 
    return 0;
}

extern timer_t sys_timer;

// 进程睡眠一段时间
// uint32 second 睡眠时间
// 成功返回0, 失败返回-1
uint64 sys_sleep()
{
    uint32 sleep_duration = 0;  // SECONDS
    uint32 start_ticks;
    spinlock_acquire(&sys_timer.lk);
    start_ticks = sys_timer.ticks;
    while (sys_timer.ticks - start_ticks < sleep_duration * 10) {
        proc_sleep(&sys_timer.ticks, &sys_timer.lk);
        spinlock_acquire(&sys_timer.lk);
    }
    spinlock_release(&sys_timer.lk);

    return 0;
}