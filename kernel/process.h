#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#include "vmm.h"

#define PROCESS_STACK_VIRT  0x7FFFFFFFE000ULL  // user stack top
#define PROCESS_STACK_PAGES 4                   // 16KB stack

typedef struct {
    uint64_t      pid;
    AddressSpace* address_space;
    uint64_t      entry;          // virtual entry point
    uint64_t      user_rsp;       // user stack pointer
} Process;

Process* process_create_from_elf(void* elf_data, uint64_t size);
void     process_run(Process* p);

#endif