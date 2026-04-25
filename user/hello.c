#include "syscall.h"

void _start(void)
{
    long pid = sys_getpid();
    (void)pid;

    sys_write("[hello] userspace online\n", 25);
    for (int i = 0; i < 8; i++) {
        sys_write("[hello] tick\n", 13);
        sys_yield();
    }

    sys_write("[hello] exit\n", 13);
    sys_exit(0);
}
