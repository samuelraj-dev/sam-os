#ifndef PMM_H
#define PMM_H

#include "types.h"
#include "bootinfo.h"

#define PAGE_SIZE 4096

void     pmm_init(BootInfo* bootInfo);
void*    pmm_alloc_page(void);
void*    pmm_alloc_pages(uint64_t count);
void     pmm_free_page(void* addr);
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_total_pages(void);

#endif