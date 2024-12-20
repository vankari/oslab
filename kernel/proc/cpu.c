#include "proc/cpu.h"
#include "riscv.h"

static cpu_t cpus[NCPU];

//此cpu的cpu struct 需要中断关闭
cpu_t* mycpu(void)
{
    int id = mycpuid();
    struct cpu *c = &cpus[id];
    return c;
}
//返回线程的cpu id 需要关闭中断
int mycpuid(void) 
{
    int id = r_tp();
    return id;
}

proc_t* myproc(void)
{
    
}