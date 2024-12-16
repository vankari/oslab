// 标准输出和报错机制
#include <stdarg.h>
#include "lib/print.h"
#include "lib/lock.h"
#include "dev/uart.h"

volatile int panicked = 0;

static spinlock_t print_lk;

static char digits[] = "0123456789abcdef";
int if_locking;
void print_init(void)
{
    spinlock_init(&print_lk ,"print_lk");
    if_locking=1;
}

void printint(long long xx,int base,int sign){
    char buf[16];
    int i;
    unsigned long long x;
    if(sign && (sign = (xx < 0)))
        x = -xx;
    else
        x = xx;

    i = 0;
    do {
        buf[i++] = digits[x % base];
    }while((x /= base) != 0);

    if(sign)
        buf[i++] = '-';

    while(--i >= 0)
        uart_putc_sync(buf[i]);
}//print一个整数

void printptr(uint64 x)
{
    int i;
    uart_putc_sync('0');
    uart_putc_sync('x');
    for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
        uart_putc_sync(digits[x >> (sizeof(uint64) * 8 - 4)]);
}//print一个指针(16进制)

// Print to the console. only understands %d, %x, %p, %s.
void printf(const char *fmt, ...)
{
    va_list ap;
    int i, c,locking;
    char *s;
    locking=if_locking;
    if(locking)
        spinlock_acquire(&print_lk);
    if (fmt == 0)
        panic("null fmt");
    va_start(ap, fmt);
    for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      uart_putc_sync(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        uart_putc_sync(*s);
      break;
    case '%':
      uart_putc_sync('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      uart_putc_sync('%');
      uart_putc_sync(c);
      break;
    }
  }
    va_end(ap);
    if(locking)
        spinlock_release(&print_lk);
}

void panic(const char *s)
{
    if_locking=0;
    printf("panic: ");
    printf("%s\n", s);
    panicked = 1; 
    for(;;);
}

void assert(bool condition, const char* warning)
{
    if(!condition)
        printf("%s\n",warning);
}
