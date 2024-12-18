#include "lib/lock.h"
#include "lib/print.h"
#include "proc/cpu.h"
#include "riscv.h"

// 带层数叠加的关中断
// 层数也相当于持有锁的数量
void push_off(void)
{
    int before_status = intr_get();
    intr_off();
    if(mycpu()->noff == 0)
        mycpu()->origin = before_status;//获取锁之前的中断开关状态
    mycpu()->noff += 1;//层数+1
}

// 带层数叠加的开中断
void pop_off(void)
{
    struct cpu *c = mycpu();
    if(intr_get())
        panic("pop_off - interruptible");
    if(c->noff < 1)
        panic("pop_off"); //未持有锁时popoff或者在持有锁时中断处于开状态抛出panic
    c->noff -= 1; //层数-1
    if(c->noff == 0 && c->origin)
        intr_on();//恢复中断状态
}

// 是否持有自旋锁
// 中断应当是关闭的
bool spinlock_holding(spinlock_t *lk)
{
    return lk->locked && lk->cpuid == mycpuid(); //locked=1且持有lock的cpu为本cpu
}

// 自选锁初始化
void spinlock_init(spinlock_t *lk, char *name)
{
    lk->name = name;
    lk->locked = 0;
    lk->cpuid = -1;
}

// 获取自选锁
void spinlock_acquire(spinlock_t *lk)
{    
    push_off(); 
    if(spinlock_holding(lk))
    panic("acquired");//已持有 panic
    //__sync_lock_test_and_set gcc提供 实现原子读写操作(将指定内存位置的值设置为新的值并返回原来的值)
    //防止在并行执行时访问+写入造成数据不安全
    while(__sync_lock_test_and_set(&lk->locked, 1) != 0);
    // 确保编译优化时不会越过这一点
    //在多线程环境下实现同步访存以防数据不安全
    __sync_synchronize();
    // for debug 记录可供debug用的信息
    lk->cpuid = mycpuid();
} 

// 释放自旋锁
void spinlock_release(spinlock_t *lk)
{
    if(!spinlock_holding(lk))
        panic("release");//未持锁释放抛出panic
    lk->cpuid = -1;
    //同步访存
    __sync_synchronize();
    //原子读写
    __sync_lock_release(&lk->locked);
    pop_off();
}