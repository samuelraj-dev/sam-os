// #include "paging.h"
// #include "display.h"
// #include "types.h"

// #define PAGE_SIZE 4096
// #define ENTRIES 512

// static uint64_t* pml4;
// static uint64_t* pdpt;

// static uint64_t next_free_table = 0x200000;

// void* alloc_table() {
//     void* addr = (void*)next_free_table;
//     next_free_table += PAGE_SIZE;
//     return addr;
// }

// void paging_init(BootInfo* bootInfo)
// {
//     pml4 = alloc_table();
//     pdpt = alloc_table();

//     // clear tables
//     for (int i = 0; i < ENTRIES; i++) {
//         pml4[i] = 0;
//         pdpt[i] = 0;
//     }

//     pml4[0] = (uint64_t)pdpt | 0x3;

//     // compute max physical address
//     uint8_t* mmap = (uint8_t*)bootInfo->memory_map;
//     uint64_t max_memory = 0;

//     for (uint64_t offset = 0;
//          offset < bootInfo->memory_map_size;
//          offset += bootInfo->memory_map_descriptor_size) {

//         uint64_t phys  = *(uint64_t*)(mmap + offset + 16);
//         uint64_t pages = *(uint64_t*)(mmap + offset + 24);

//         uint64_t end = phys + pages * PAGE_SIZE;
//         if (end > max_memory)
//             max_memory = end;
//     }
//     print("max_memory: ");
//     print_hex(max_memory);
//     print("\n");

//     uint64_t addr = 0;

//     for (uint64_t pdpt_i = 0;
//          addr < max_memory && pdpt_i < ENTRIES;
//          pdpt_i++) {

//         uint64_t* pd = alloc_table();

//         for (int i = 0; i < ENTRIES; i++)
//             pd[i] = 0;

//         pdpt[pdpt_i] = (uint64_t)pd | 0x3;

//         for (uint64_t pd_i = 0;
//              pd_i < ENTRIES && addr < max_memory;
//              pd_i++) {

//             pd[pd_i] = addr | 0x83;  // 2MB page
//             addr += 0x200000;
//         }
//     }

//     __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4) : "memory");
// }
#include "paging.h"
#include "display.h"

#include "types.h"

#define PAGE_SIZE 4096
#define ENTRIES   512
#define PSE_2MB   0x200000ULL
#define EFI_CONVENTIONAL_MEMORY 7

static uint64_t* pml4;
static uint64_t* pdpt;
static uint64_t  next_free_table;
uint64_t paging_arena_end = 0; 

static void* alloc_table(void) {
    uint8_t* addr = (uint8_t*)next_free_table;
    next_free_table += PAGE_SIZE;
    // zero it so callers don't have to
    for (int i = 0; i < PAGE_SIZE; i++) addr[i] = 0;
    return (void*)addr;
}

void paging_init(BootInfo* bootInfo) {
    // Start table arena right after the kernel image
    next_free_table = ((uint64_t)&_kernel_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

    pml4 = alloc_table();
    pdpt = alloc_table();

    print("fb pdpt index: ");
    print_dec((uint64_t)bootInfo->framebuffer / 0x40000000ULL);
    print("\n");

    pml4[0] = (uint64_t)pdpt | 0x3;

    // Compute max physical address from usable memory only
    uint8_t*  mmap  = (uint8_t*)bootInfo->memory_map;
    uint64_t  dsize = bootInfo->memory_map_descriptor_size;
    // uint64_t  max_memory = 0;

    uint64_t ram_top = 0;
    uint64_t mmio_top = 0;

    for (uint64_t off = 0; off < bootInfo->memory_map_size; off += dsize) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)(mmap + off);
        uint64_t end = desc->PhysicalStart + desc->NumberOfPages * PAGE_SIZE;
        
        // Only conventional memory counts as real RAM top
        if (desc->Type == 7 || desc->Type == 1 || desc->Type == 2) {
            if (end > ram_top) ram_top = end;
        }
        // Track MMIO separately
        if (end > mmio_top) mmio_top = end;
    }

    // Map up to RAM top, plus framebuffer if it's higher
    uint64_t fb_end = (uint64_t)bootInfo->framebuffer + 
                    bootInfo->height * bootInfo->pixels_per_scanline * 4;
    uint64_t max_memory = ram_top;
    if (fb_end > max_memory) max_memory = fb_end;

    // Round up to next 2MB
    max_memory = (max_memory + PSE_2MB - 1) & ~(uint64_t)(PSE_2MB - 1);

    print("max_memory: ");
    print_hex(max_memory);
    print("\n");

    uint64_t addr = 0;
    for (uint64_t pdpt_i = 0; addr < max_memory && pdpt_i < ENTRIES; pdpt_i++) {
        uint64_t* pd = alloc_table();
        pdpt[pdpt_i] = (uint64_t)pd | 0x3;

        for (uint64_t pd_i = 0; pd_i < ENTRIES && addr < max_memory; pd_i++) {
            // pd[pd_i] = addr | 0x83; // Present + Writable + 2MB PS
            pd[pd_i] = addr | 0x83 | (1 << 3);
            addr += PSE_2MB;
        }
    }
    // uint64_t* first_pd = (uint64_t*)(pdpt[0] & ~0xFFFULL);
    // first_pd[0] = 0;

    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4) : "memory");

    
    // Write one pixel immediately after CR3 switch
    // If screen changes, paging is fine
    // If machine resets, framebuffer isn't mapped
    
    paging_arena_end = next_free_table;
}
