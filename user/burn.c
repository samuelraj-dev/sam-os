#include "syscall.h"

static unsigned long fib(unsigned long n)
{
    unsigned long a = 0;
    unsigned long b = 1;
    for (unsigned long i = 0; i < n; i++) {
        unsigned long t = a + b;
        a = b;
        b = t;
    }
    return a;
}

void _start(void)
{
    sys_write("[burn] cpu-bound start\n", 23);

    for (int round = 0; round < 12; round++) {
        volatile unsigned long acc = 0;
        for (unsigned long i = 0; i < 50000; i++) {
            acc ^= fib((i % 24) + 8);
        }
        (void)acc;

        if ((round % 3) == 0)
            sys_write("[burn] still running\n", 21);

        sys_yield();
    }

    sys_write("[burn] exit\n", 12);
    sys_exit(0);
}
