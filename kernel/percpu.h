#ifndef PERCPU_H
#define PERCPU_H

#include "types.h"

// must match offsets used in syscall_entry.S
typedef struct __attribute__((packed)) {
    uint64_t kernel_rsp;  // offset 0
    uint64_t user_rsp;    // offset 8
} PerCPU;

void percpu_init(void);

#endif