#include "mem/mmap.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/str.h"
#include "memlayout.h"

// 连续虚拟空间的复制(在uvm_copy_pgtbl中使用)
static void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t* pte;

    for(va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");
        
        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        page = (uint64)pmem_alloc(false);
        memmove((char*)page, (const char*)pa, PGSIZE);
        vm_mappages(new, va, page, PGSIZE, flags);
    }
}

// 两个 mmap_region 区域合并
// 保留一个 释放一个 不操作 next 指针
// 在uvm_munmap里使用
static void mmap_merge(mmap_region_t* mmap_1, mmap_region_t* mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");
    
    // merge
    if(keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
}

// 打印以 mmap 为首的 mmap 链
// for debug
void uvm_show_mmaplist(mmap_region_t* mmap)
{
    mmap_region_t* tmp = mmap;
    printf("\nmmap allocable area:\n");
    if(tmp == NULL)
        printf("NULL\n");
    while(tmp != NULL) {
        printf("allocable region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
}

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 3, level = 0 说明是页表管理的物理页
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
    if(level==0){
        pmem_free((uint64)pgtbl,false);
    }
    else{
        level--;
        for(int i=0;i<PGSIZE/sizeof(pte_t);i++)//遍历pte
        {
            pte_t pte = pgtbl[i]; 
            if(pte & PTE_V) {   // 有效PTE
            // 更新pagetable指向下一级页表
                pgtbl_t nxtpgtbl = (pgtbl_t)PTE_TO_PA(pte);
                destroy_pgtbl(nxtpgtbl,level);
                pgtbl[i]=0;
            }
        }
        if(level<2)pmem_free((uint64)pgtbl,true);
        if(level==2)pmem_free((uint64)pgtbl,false);
    }

}

// 页表销毁：trapframe 和 trampoline 单独处理
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    vm_unmappages(pgtbl,TRAMPOLINE,PGSIZE,false);
    vm_unmappages(pgtbl,TRAPFRAME,PGSIZE,true);
    destroy_pgtbl(pgtbl,3);
    
}

// 拷贝页表 (拷贝并不包括trapframe 和 trampoline)
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint32 ustack_pages, mmap_region_t* mmap)
{
    /* step-1: USER_BASE ~ heap_top */
    copy_range(old,new,USER_BASE,heap_top);
    /* step-2: ustack */
    copy_range(old,new,TRAPFRAME-ustack_pages*PGSIZE,TRAPFRAME);
    /* step-3: mmap_region */
    uint64 alloc_start =MMAP_BEGIN;
    for (mmap_region_t *s = mmap; s != NULL; s = s->next) {
        while (alloc_start < s->begin) {
            copy_range(old, new, alloc_start, alloc_start + PGSIZE);
            alloc_start += PGSIZE;
        }
    }

}

// 在用户页表和进程mmap链里 新增mmap区域 [begin, begin + npages * PGSIZE)
// 页面权限为perm
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_mmap: begin not aligned");

    // 修改 mmap 链 (分情况的链式操作)
    proc_t* p= myproc();
    mmap_region_t* tmp =p->mmap;
    mmap_region_t* todel =NULL;//一块可分配空间
    while(tmp!=NULL){
        if(tmp->begin+tmp->npages*PGSIZE>begin+npages*PGSIZE&&tmp->begin<begin){
            mmap_region_t* new=mmap_region_alloc();
            new->begin=begin+npages*PGSIZE;
            new->npages=(tmp->begin+tmp->npages*PGSIZE-begin-npages*PGSIZE)/PGSIZE;
            tmp->npages=(begin-tmp->begin)/PGSIZE;
            new->next =tmp->next;
            tmp->next = new;
            break;
        }
        else if(tmp->begin+tmp->npages*PGSIZE == begin+npages*PGSIZE&&tmp->begin<begin){
            tmp->npages=tmp->npages-npages;
            break;
        }//留前
        else if(tmp->begin+tmp->npages*PGSIZE>begin+npages*PGSIZE&&tmp->begin==begin){
            tmp->begin=begin+npages*PGSIZE;
            break;
        }//留后
        else if(tmp->begin+tmp->npages*PGSIZE==begin+npages*PGSIZE&&tmp->begin==begin)
        {
            todel=tmp;
            break;
        }//删除一整块
        tmp=tmp->next;
    }
    if(todel!=NULL){
        mmap_region_t* tmp = p->mmap;
        while(tmp!=NULL){
            if(tmp->next==todel)break;
            tmp=tmp->next;
        }
        if(tmp!=NULL)tmp->next=todel->next;
        else{
            p->mmap=todel->next;
        }
        mmap_region_free(todel);
    }
    if(tmp==NULL)panic("mmaperror");
    // 修改页表 (物理页申请 + 页表映射)
    uint64 va = begin;
    for (uint32 i = 0; i < npages; i++) {
        void *page = pmem_alloc(false);
        if (!page) panic("uvm_mmap: failed to allocate physical page");
        vm_mappages(p->pgtbl, va, (uint64)page, PGSIZE, perm|PTE_R|PTE_W|PTE_U);
        va += PGSIZE;
    }
}

// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
void uvm_munmap(uint64 begin, uint32 npages)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_munmap: begin not aligned");

    // new mmap_region 的产生
    proc_t* p= myproc();
    mmap_region_t* tmp =p->mmap;
    mmap_region_t* new = mmap_region_alloc();
    new->begin=begin;
    new->npages=npages;
    bool complete = false; //是否能插入
    if(tmp==NULL){
        p->mmap=new;
        complete=true;
    }
    // 尝试合并 mmap_region
    if(new->begin+new->npages*PGSIZE<=tmp->begin &&new->begin>=MMAP_BEGIN){
        new->next = tmp;
        p->mmap = new;
        complete = true;
    }//插头
    while (tmp!=NULL)
    {
        if(tmp->next!=NULL&& new->begin>=tmp->begin+tmp->npages*PGSIZE&&new->begin+new->npages*PGSIZE<=tmp->next->begin)
        {
            new->next = tmp->next;
            tmp->next=new;
            complete =true;
            break;
        }
        else if(tmp->next==NULL && new->begin>=tmp->begin+tmp->npages*PGSIZE &&new->begin+new->npages*PGSIZE<=MMAP_END)
        {
            new->next = NULL;
            tmp->next=new;
            complete = true;
        }
    }
    //找到插入点
    if(complete==true){
        tmp=p->mmap;
        //合并
        while(tmp->next!=NULL){
            if(tmp->begin+tmp->npages*PGSIZE == tmp->next->begin){
                mmap_region_t* todel= tmp->next;
                tmp->next=todel->next;
                mmap_merge(tmp,todel,true);
            }
        }
    }
    else{
        mmap_region_free(new);
        panic("munmaperror");
    }
    // 页表释放
    for(uint64 va=begin;va<begin+npages*PGSIZE;va+=PGSIZE){
        pte_t *pte = vm_getpte(p->pgtbl, va, 0);
        if (pte && (*pte & PTE_V)) {
            vm_unmappages(p->pgtbl,va,PGSIZE,true);
        }
    }
}

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
// 在这里无需修正 p->heap_top
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top + len;
    assert(new_heap_top<TRAPFRAME-PGSIZE,"heapoverflow");
    uint64 va = ALIGN_UP(heap_top,PGSIZE);
    for(uint64 cur_pg=va;cur_pg<new_heap_top;cur_pg+=PGSIZE){
        uint64 pa =(uint64) pmem_alloc(false);
        if((void*)pa==NULL)panic("nomorephys");
        memset((void*)pa,0,PGSIZE);
        vm_mappages(pgtbl,cur_pg,pa,PGSIZE,PTE_R|PTE_W|PTE_U);
    }
    

    return new_heap_top;
}

// 用户堆空间减少, 返回新的堆顶地址
// 在这里无需修正 p->heap_top
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top - len;
    assert(new_heap_top>=2*PGSIZE,"heaperror");
    uint64 up_heap_top = ALIGN_UP(heap_top, PGSIZE);
    uint64 up_new_heap_top = ALIGN_UP(new_heap_top, PGSIZE); 
    if( up_new_heap_top < up_heap_top) {
        int pages = (up_heap_top - up_new_heap_top) / PGSIZE;
        vm_unmappages(pgtbl, up_new_heap_top, pages, true);
    }

    return new_heap_top;
}

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 va,pa,len0;
    while(len >0){
        va = ALIGN_DOWN(src,PGSIZE);
        pte_t* pte = vm_getpte(pgtbl,va,false);//失败不申请物理页
        if(pte==NULL) panic("COPYinPTEerror");
        pa = PTE_TO_PA(*pte);
        len0=PGSIZE-(src-va);
        if(len0>len)len0=len;
        memmove((void*)(dst),(void*)(pa+src-va),len0);
        //调整下次移动的位置
        len-=len0;
        src+=len0;
        dst+=len0;
    }
}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 va,pa,len0;
    while(len >0){
        va = ALIGN_DOWN(dst,PGSIZE);
        pte_t* pte = vm_getpte(pgtbl,va,false);//失败不申请物理页
        if(pte==NULL) panic("COPYoutPTEerror");
        pa = PTE_TO_PA(*pte);
        len0=PGSIZE-(dst-va);
        if(len0>len)len0=len;
        memmove((void*)(pa+(dst-va)),(void*)src,len0);
        //调整下次移动的位置
        len-=len0;
        src+=len0;
        dst+=len0;
    }
}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    uint64 va,pa,len0;
    bool isnull=false;
    char* c;
    while(!isnull && maxlen >0){
        va = ALIGN_DOWN(src,PGSIZE);
        pte_t* pte = vm_getpte(pgtbl,va,false);//失败不申请物理页
        if(pte==NULL) panic("strCOPYinTEerror");
        pa = PTE_TO_PA(*pte);
        len0=PGSIZE-(src-va);
        if(len0>maxlen)len0=maxlen;
        c = (char*)(pa+src-va);
        for(int i=0;i<len0;i++){
            *((char*)(dst+i)) = c[i];
            if(c[i]=='\0')
            {
                isnull=true;
                break;
            }
        }
        //调整下次移动的位置
        maxlen-=len0;
        src+=len0;
        dst+=len0;
    }
}