#include "lib/print.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "mem/vmem.h"
#include "memlayout.h"
#include "riscv.h"

// in trampoline.S
extern char trampoline[];      // 内核和用户切换的代码
extern char user_vector[];     // 用户触发trap进入内核
extern char user_return[];     // trap处理完毕返回用户

// in trap.S
extern char kernel_vector[];   // 内核态trap处理流程

// in trap_kernel.c
extern char* interrupt_info[16]; // 中断错误信息
extern char* exception_info[16]; // 异常错误信息

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)
    proc_t* p = myproc();

    // 确认trap来自U-mode
    assert((sstatus & SSTATUS_SPP) == 0, "trap_user_handler: not from u-mode");
    p->tf->epc = sepc;
    uint64 trapid = scause & 0xf;
    if(scause & 0x8000000000000000){
        switch(trapid){
            case 1://smode 软中断 m mode timer intr引起
                timer_interrupt_handler();
                break;
            case 5:
                printf("scause = %p\n",scause);
                printf("sepc = %p\n",sepc);
                printf("stval = %p\n",stval);
                break;
            case 9:
                external_interrupt_handler();
                break;
            default:
                printf("intrcode = %d\n",trapid);
                printf("%s\n",interrupt_info[trapid]);
                break;
        }
    }
    else{
        switch(trapid){
            case 8://syscall 简单回应
                printf("get a syscall from proc %d\n",p->pid);
                p->tf->epc+=4;
                intr_on();
                break;
            default:
                printf("excecode = %d\n",trapid);
                printf("%s\n",exception_info[trapid]);
                break;
        }
    }
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
void trap_user_return()
{
    proc_t* p=myproc();
    intr_off();

    // send syscalls, interrupts, and exceptions to trampoline.S
    //trampoline.S中定义的usrvector的位置
    w_stvec(TRAMPOLINE + ((uint64)user_vector - (uint64)trampoline));

    // set up trapframe values that uservec will need when
    // the process next re-enters the kernel.
    p->tf->kernel_satp = r_satp();         // kernel page table
    p->tf->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
    p->tf->kernel_trap = (uint64)trap_user_handler;
    p->tf->kernel_hartid = r_tp();         // hartid for cpuid()

    // set up the registers that trampoline.S's sret will use
    // to get to user space.
    //以下都在设置smode下返回umode涉及的寄存器
    // set S Previous Privilege mode to User.
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
    x |= SSTATUS_SPIE; // enable interrupts in user mode
    w_sstatus(x);

    // set S Exception Program Counter to the saved user pc.
    w_sepc(p->tf->epc);

    // tell trampoline.S the user page table to switch to.
    uint64 satp = MAKE_SATP(p->pgtbl);

    // jump to trampoline.S at the top of memory, which 
    // switches to the user page table, restores user registers,
    // and switches to user mode with sret.
    uint64 fn = TRAMPOLINE + ((uint64)user_return - (uint64)trampoline);
    //user_return()，切换用户页表并刷新tlb 然后sret
    ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}