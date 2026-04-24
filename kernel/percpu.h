#ifndef PERCPU_H
#define PERCPU_H

#include "types.h"

// must match offsets used in syscall_entry.S
typedef struct __attribute__((packed)) {
    uint64_t kernel_rsp;   // offset 0
    uint64_t user_rsp;     // offset 8
    uint64_t kernel_cr3;   // offset 16
    uint64_t user_cr3;     // offset 24
} PerCPU;

extern PerCPU percpu_data;

void percpu_init(void);

#endif