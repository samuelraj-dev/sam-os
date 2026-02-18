#ifndef PMM_H
#define PMM_H

#include "bootinfo.h"

void pmm_init(BootInfo* bootInfo);
void* pmm_alloc_page();
void  pmm_free_page(void* addr);

#endif