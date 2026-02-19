#ifndef PAGING_H
#define PAGING_H

#include "bootinfo.h"

#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL
#define KERNEL_PHYS_BASE 0x100000ULL

extern uint64_t paging_arena_end;

void paging_init(BootInfo* bootInfo);
void paging_remove_identity_map(void);

#endif