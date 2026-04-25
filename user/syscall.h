#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include "../kernel/syscall_abi.h"

static inline unsigned long __syscall3(unsigned long num,
                                       unsigned long a0,
                                       unsigned long a1,
                                       unsigned long a2)
{
    unsigned long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a0), "c"(a1), "d"(a2)
        : "memory"
    );
    return ret;
}

static inline unsigned long sys_write(const char* s, unsigned long len)
{
    return __syscall3(SYS_WRITE, (unsigned long)s, len, 0);
}

static inline void sys_exit(int code)
{
    (void)__syscall3(SYS_EXIT, (unsigned long)code, 0, 0);
    while (1) { }
}

static inline void sys_yield(void)
{
    (void)__syscall3(SYS_YIELD, 0, 0, 0);
}

static inline long sys_spawn(int image_id)
{
    return (long)__syscall3(SYS_SPAWN, (unsigned long)image_id, 0, 0);
}

static inline long sys_getpid(void)
{
    return (long)__syscall3(SYS_GETPID, 0, 0, 0);
}

#endif
