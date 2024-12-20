#include "riscv.h"
#include "lib/print.h"
#include "lib/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/proc.h"
#include "trap/trap.h"

volatile static int started = 0;
/*
int main()
{
    if(mycpuid()==0){
        print_init();
        printf("cpu %d starting\n", mycpuid());
        __sync_synchronize();
        started=1;
    }
    else{
        while(started == 0);
        __sync_synchronize();
        printf("cpu %d starting\n", mycpuid());
    }
    return 0;
}

#include "lib/str.h"
#include "mem/pmem.h"
volatile static int started = 0;

//volatile static int over_1 = 0, over_2 = 0;

static int* mem[1024];
*/
int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        print_init();
        printf("cpu %d is booting!\n", cpuid);

        pmem_init();
        kvm_init();
        kvm_inithart();
        trap_kernel_init();
        trap_kernel_inithart();

        proc_make_fisrt();
        
        __sync_synchronize();
        // started = 1;

    } else {

        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        kvm_inithart();
        trap_kernel_inithart();
    }
    while (1);    
}
