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
    uint64_t kernel_end_virt = (uint64_t)&_kernel_end;
    uint64_t kernel_end_phys = kernel_end_virt - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE;
    next_free_table = (kernel_end_phys + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

    pml4 = alloc_table();
    pdpt = alloc_table();

    // identity map — pml4[0]
    // TODO: remove after VMM is solid
    pml4[0] = (uint64_t)pdpt | 0x3;

    uint8_t*  mmap  = (uint8_t*)bootInfo->memory_map;
    uint64_t  dsize = bootInfo->memory_map_descriptor_size;
    uint64_t  ram_top = 0;

    for (uint64_t off = 0; off < bootInfo->memory_map_size; off += dsize) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)(mmap + off);
        uint64_t end = desc->PhysicalStart + desc->NumberOfPages * PAGE_SIZE;
        if (desc->Type == 7 || desc->Type == 1 || desc->Type == 2)
            if (end > ram_top) ram_top = end;
    }

    uint64_t fb_end = (uint64_t)bootInfo->framebuffer +
                      bootInfo->height * bootInfo->pixels_per_scanline * 4;
    uint64_t max_memory = ram_top;
    if (fb_end > max_memory) max_memory = fb_end;
    max_memory = (max_memory + PSE_2MB - 1) & ~(uint64_t)(PSE_2MB - 1);

    uint64_t addr = 0;
    for (uint64_t pdpt_i = 0; addr < max_memory && pdpt_i < ENTRIES; pdpt_i++) {
        uint64_t* pd = alloc_table();
        pdpt[pdpt_i] = (uint64_t)pd | 0x3;
        for (uint64_t pd_i = 0; pd_i < ENTRIES && addr < max_memory; pd_i++) {
            pd[pd_i] = addr | 0x83;
            addr += PSE_2MB;
        }
    }

    // higher-half mapping — kernel virtual → physical 0x0
    uint64_t* pdpt_hi = alloc_table();
    pml4[511] = (uint64_t)pdpt_hi | 0x3;

    uint64_t* pd_hi = alloc_table();
    pdpt_hi[510] = (uint64_t)pd_hi | 0x3;

    uint64_t phys = 0;
    for (int i = 0; i < 512; i++) {
        pd_hi[i] = phys | 0x83;
        phys += PSE_2MB;
    }

    __asm__ volatile ("cli");
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4) : "memory");

    paging_arena_end = next_free_table;
}

void paging_remove_identity_map(void)
{
    pml4[0] = 0;
    // flush TLB
    __asm__ volatile (
        "mov %%cr3, %%rax\n"
        "mov %%rax, %%cr3\n"
        ::: "rax", "memory"
    );
}