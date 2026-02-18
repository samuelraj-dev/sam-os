#include "paging.h"
#include "types.h"

#define PAGE_SIZE 4096
#define ENTRIES 512

static uint64_t* pml4;
static uint64_t* pdpt;

static uint64_t next_free_table = 0x200000;

void* alloc_table() {
    void* addr = (void*)next_free_table;
    next_free_table += PAGE_SIZE;
    return addr;
}

void paging_init(BootInfo* bootInfo)
{
    pml4 = alloc_table();
    pdpt = alloc_table();

    // clear tables
    for (int i = 0; i < ENTRIES; i++) {
        pml4[i] = 0;
        pdpt[i] = 0;
    }

    pml4[0] = (uint64_t)pdpt | 0x3;

    // compute max physical address
    uint8_t* mmap = (uint8_t*)bootInfo->memory_map;
    uint64_t max_memory = 0;

    for (uint64_t offset = 0;
         offset < bootInfo->memory_map_size;
         offset += bootInfo->memory_map_descriptor_size) {

        uint64_t phys  = *(uint64_t*)(mmap + offset + 16);
        uint64_t pages = *(uint64_t*)(mmap + offset + 24);

        uint64_t end = phys + pages * PAGE_SIZE;
        if (end > max_memory)
            max_memory = end;
    }

    uint64_t addr = 0;

    for (uint64_t pdpt_i = 0;
         addr < max_memory && pdpt_i < ENTRIES;
         pdpt_i++) {

        uint64_t* pd = alloc_table();

        for (int i = 0; i < ENTRIES; i++)
            pd[i] = 0;

        pdpt[pdpt_i] = (uint64_t)pd | 0x3;

        for (uint64_t pd_i = 0;
             pd_i < ENTRIES && addr < max_memory;
             pd_i++) {

            pd[pd_i] = addr | 0x83;  // 2MB page
            addr += 0x200000;
        }
    }

    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4) : "memory");
}