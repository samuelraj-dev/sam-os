#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"

#define SYS_WRITE  0
#define SYS_EXIT   1

void syscall_init(void);

#endif