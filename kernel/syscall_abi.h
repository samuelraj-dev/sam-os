#ifndef SYSCALL_ABI_H
#define SYSCALL_ABI_H

#include "types.h"

enum {
    SYS_WRITE = 0,
    SYS_EXIT  = 1,
    SYS_YIELD = 2,
    SYS_SPAWN = 3,
    SYS_GETPID = 4,
};

enum {
    SPAWN_IMAGE_HELLO = 0,
    SPAWN_IMAGE_BURN  = 1,
};

#endif
