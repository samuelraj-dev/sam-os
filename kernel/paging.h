#ifndef PAGING_H
#define PAGING_H

#include "bootinfo.h"

extern uint64_t paging_arena_end;

void paging_init(BootInfo* bootInfo);

#endif