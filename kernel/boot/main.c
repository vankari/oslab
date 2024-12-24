#include "riscv.h"
#include "lib/print.h"
#include "proc/cpu.h"
volatile static int started = 0;
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