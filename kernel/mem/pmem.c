#include "mem/pmem.h"
#include "lib/print.h"
#include "lib/lock.h"
#include "lib/str.h"
#include "proc/cpu.h"
// 物理页节点
typedef struct page_node {
    struct page_node* next;
} page_node_t;

// 许多物理页构成一个可分配的区域
typedef struct alloc_region {
    uint64 begin;          // 起始物理地址
    uint64 end;            // 终止物理地址
    spinlock_t lk;         // 自旋锁(保护下面两个变量)
    uint32 allocable;      // 可分配页面数    
    page_node_t list_head; // 可分配链的链头节点
} alloc_region_t;

// 内核和用户可分配的物理页分开
static alloc_region_t kern_region, user_region;

#define KERN_PAGES 1024 // 内核可分配空间占1024个pages

// 物理内存初始化
void pmem_init(void)
{
    spinlock_init(&kern_region.lk,"kern_region_lock");
    spinlock_init(&user_region.lk,"user_region_lock");
    spinlock_acquire(&kern_region.lk);
    kern_region.begin = (uint64 )ALLOC_BEGIN;
    kern_region.end = (uint64)ALLOC_BEGIN+KERN_PAGES*PGSIZE;
    kern_region.allocable = KERN_PAGES;//宏优化
    kern_region.list_head.next = (page_node_t*)kern_region.begin;
    page_node_t* temp =kern_region.list_head.next;
    for(uint64 p=kern_region.begin+PGSIZE;p<kern_region.end;)
    {
        temp->next =(page_node_t*)p;
        temp = (page_node_t*)p;
        p+=PGSIZE;
    }
    temp->next=NULL;
    spinlock_release(&kern_region.lk);
    spinlock_acquire(&user_region.lk);
    user_region.begin = (uint64)ALLOC_BEGIN+KERN_PAGES*PGSIZE;
    user_region.end = (uint64)ALLOC_END;
    user_region.allocable = ((uint64)ALLOC_END-(uint64)ALLOC_BEGIN-KERN_PAGES*PGSIZE)/PGSIZE;//宏优化
    user_region.list_head.next = (page_node_t*)user_region.begin;
    page_node_t* temp1 =user_region.list_head.next;
    for(uint64 p=user_region.begin+PGSIZE;p<user_region.end;)
    {
        temp1->next =(page_node_t*)p;
        temp1 = (page_node_t*)p;
        p+=PGSIZE;
    }
    temp1->next=NULL;
    spinlock_release(&user_region.lk);
}

// 返回一个可分配的干净物理页
// 失败则panic锁死
void* pmem_alloc(bool in_kernel)
{
    page_node_t* ret;
    if(in_kernel)
    {  
        spinlock_acquire(&kern_region.lk);
        ret=kern_region.list_head.next;
        uint64 begin=kern_region.begin;
        uint64 end=kern_region.end;
        if(((uint64)ret % PGSIZE) != 0 || (uint64)ret < begin || (uint64)ret >= end)
            panic("kern_pmem_alloc");
        kern_region.list_head.next = ret->next;
        spinlock_release(&kern_region.lk);
    }
    else
    {
        spinlock_acquire(&user_region.lk);
        ret=user_region.list_head.next;
        uint64 begin=user_region.begin;
        uint64 end=user_region.end;
        if(((uint64)ret % PGSIZE) != 0 || (uint64)ret < begin || (uint64)ret >= end)
            panic("user_pmem_alloc");
        user_region.list_head.next = ret->next;
        spinlock_release(&user_region.lk);    
    }
    return (void*)ret;
}

// 释放物理页
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel)
{
    if(in_kernel)
    {
        spinlock_acquire(&kern_region.lk);
        uint64 begin=kern_region.begin;
        uint64 end=kern_region.end;
        if((page % PGSIZE) != 0 || page < begin || page >= end)
            panic("kern_pmem_free");
        page_node_t* temp = (page_node_t* )page;
        temp->next = kern_region.list_head.next;
        kern_region.list_head.next = temp;
        spinlock_release(&kern_region.lk);
    }
    else
    {
        spinlock_acquire(&user_region.lk);
        uint64 begin=user_region.begin;
        uint64 end=user_region.end;
        if((page % PGSIZE) != 0 || page < begin || page >= end)
            panic("user_pmem_free");
        page_node_t* temp = (page_node_t* )page;
        temp->next = user_region.list_head.next;
        user_region.list_head.next = temp;
        spinlock_release(&user_region.lk);
    }
}