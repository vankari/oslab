#include "lib/print.h"
#include "lib/str.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/mmap.h"

#include "memlayout.h"
// 包装 mmap_region_t 用于仓库组织
typedef struct mmap_region_node {
    mmap_region_t mmap;
    struct mmap_region_node* next;
} mmap_region_node_t;

#define N_MMAP 256

// mmap_region_node_t 仓库(单向链表) + 指向链表头节点的指针 + 保护仓库的锁
static mmap_region_node_t list_mmap_region_node[N_MMAP];
static mmap_region_node_t* list_head;
static spinlock_t list_lk;

// 初始化上述三个数据结构
void mmap_init()
{
    spinlock_init(&list_lk,"mmap_list_lk");
    for(int i=0;i<N_MMAP;i++){
        list_mmap_region_node[i].mmap.begin=0;
        list_mmap_region_node[i].mmap.npages=0;
        if(i==N_MMAP-1)
            list_mmap_region_node[i].next=NULL;
        else
            list_mmap_region_node[i].next=&list_mmap_region_node[i+1];
    }
    list_head=list_mmap_region_node;
}

// 从仓库申请一个 mmap_region_t
// 若申请失败则 panic
// 注意: list_head 保留, 不会被申请出去
mmap_region_t* mmap_region_alloc()
{
    spinlock_acquire(&list_lk);
    mmap_region_t* ret=NULL;
    mmap_region_node_t* tmp=list_head->next;
    while(tmp!=NULL){
        if(tmp->mmap.begin==0)break;
        tmp=tmp->next;
    }
    list_head->next=tmp->next;
    ret=&tmp->mmap;
    ret->begin=MMAP_BEGIN;
    ret->npages=(MMAP_END-MMAP_BEGIN)/PGSIZE;
    ret->next=NULL;
    spinlock_release(&list_lk);
    if(ret==NULL)
        panic("mmapallocerror");
    return ret;
}

// 向仓库归还一个 mmap_region_t
void mmap_region_free(mmap_region_t* mmap)
{
    spinlock_acquire(&list_lk);
    mmap->begin = 0;
    mmap->npages = 0;
    mmap->next = NULL;
    mmap_region_node_t* tmp=NULL;
    for(int i=0;i<N_MMAP;i++){
        if(&list_head[i].mmap==mmap)
            tmp=&list_head[i];
    }
    tmp->next=list_head->next;
    list_head->next=tmp;
    spinlock_release(&list_lk);
}

// 输出仓库里可用的 mmap_region_node_t
// for debug
void mmap_show_mmaplist()
{
    spinlock_acquire(&list_lk);
    
    mmap_region_node_t* tmp = list_head;
    int node = 1, index = 0;
    while (tmp)
    {
        index = tmp - list_head;
        printf("node %d index = %d\n", node++, index);
        tmp = tmp->next;
    }

    spinlock_release(&list_lk);
}