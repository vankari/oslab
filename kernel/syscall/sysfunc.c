#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/mmap.h"
#include "lib/str.h"
#include "lib/print.h"
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
// uint64 start 起始地址 (如果为0则由内核自主选择一个合适的起点, 通常是顺序扫描找到一个够大的空闲空间)
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

// copyin 测试 (int 数组)
// uint64 addr
// uint32 len
// 返回 0
uint64 sys_copyin()
{
    proc_t* p = myproc();
    uint64 addr;
    uint32 len;

    arg_uint64(0, &addr);
    arg_uint32(1, &len);

    int tmp;
    for(int i = 0; i < len; i++) {
        uvm_copyin(p->pgtbl, (uint64)&tmp, addr + i * sizeof(int), sizeof(int));
        printf("get a number from user: %d\n", tmp);
    }

    return 0;
}

// copyout 测试 (int 数组)
// uint64 addr
// 返回数组元素数量
uint64 sys_copyout()
{
    int L[5] = {1, 2, 3, 4, 5};
    proc_t* p = myproc();
    uint64 addr;

    arg_uint64(0, &addr);
    uvm_copyout(p->pgtbl, addr, (uint64)L, sizeof(int) * 5);

    return 5;
}

// copyinstr测试
// uint64 addr
// 成功返回0
uint64 sys_copyinstr()
{
    char s[64];

    arg_str(0, s, 64);
    printf("get str from user: %s\n", s);

    return 0;
}
