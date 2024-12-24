#include "lib/lock.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "memlayout.h"
#include "riscv.h"

/*-------------------- 工作在M-mode --------------------*/

// in trap.S M-mode时钟中断处理流程()
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间(考虑为什么可以这么写)
static uint64 mscratch[NCPU][5];

// 时钟初始化
// called in start.c
void timer_init()
{
    // each CPU has a separate source of timer interrupts.
  int id = r_mhartid();

  // ask the CLINT for a timer interrupt.
  int interval = 1000000; // cycles; about 1/10th second in qemu.
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;

  uint64 *scratch = mscratch[id];
  scratch[3] = CLINT_MTIMECMP(id);
  scratch[4] = interval;
  w_mscratch((uint64)scratch);

  // set the machine-mode trap handler.
  w_mtvec((uint64)timer_vector);

  // enable machine-mode interrupts.
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // enable machine-mode timer interrupts.
  w_mie(r_mie() | MIE_MTIE);
}


/*--------------------- 工作在S-mode --------------------*/

// 系统时钟
static timer_t sys_timer;

// 时钟创建(初始化系统时钟)
void timer_create()
{
    spinlock_init(&sys_timer.lk,"timer");
    sys_timer.ticks=0;
}

// 时钟更新(ticks++ with lock)
void timer_update()
{
    spinlock_acquire(&sys_timer.lk);
 //   printf("cpu%d tick %d\n",r_tp(),sys_timer.ticks);
    sys_timer.ticks++;
    spinlock_release(&sys_timer.lk);
}

// 返回系统时钟ticks
uint64 timer_get_ticks()
{
    return sys_timer.ticks;
}