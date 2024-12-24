#include "riscv.h"
#include "dev/timer.h"
__attribute__ ((aligned (16))) uint8 CPU_stack[4096 * NCPU];
int main();
void start()
{
    /*riscv中定义了三种运行模式:
    machine mode:通常用来执行bootloader或固件
    supervisor mode:通常用来执行内核程序
    user mode:通常运行用户程序
    mret sret uret分别用于从上面三种模式的trap返回，返回值为M/S/UPP(previous previledge记录的值)
    */
    //进入surpervisor模式(设置MPP为supervisor)
    unsigned long x = r_mstatus();
    x &= ~MSTATUS_MPP_MASK;
    x |= MSTATUS_MPP_S;
    w_mstatus(x); //设置M mode下mstatus寄存器的MPP状态位为01，这样执行完mret后就会进入S mode
    //之后跳转到main
    w_mepc((uint64)main);
    //关闭内存分页，直接使用物理地址
    //设置所有中断异常处理在S mode下 16bit
    w_satp(0);
    w_medeleg(0xffff);
    w_mideleg(0xffff);
    w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);//设置SIE(S mode 中断使能)的SEIE STIE SSIE
    timer_init();
    /*
    //SEIE 外部中断 STIE 定时器中断 SSIE 软中断，对应使能位为1时对应中断被使能
    // 设置物理内存保护并让smode获取物理内存(全部)访问权
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);
    */
    //将CPU的hartid保存到tp寄存器
    int hartid = r_mhartid();
    w_tp(hartid);
    //调用mret
    asm volatile("mret");

}