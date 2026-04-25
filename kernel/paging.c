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

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITE    (1ULL << 1)
#define PAGE_PCD      (1ULL << 4)
#define PAGE_HUGE     (1ULL << 7)
#define PAGE_NX       (1ULL << 63)
#define IDENTITY_MIN_4G (1ULL << 32)

static uint64_t* pml4;
static uint64_t* pdpt;
static uint64_t  next_free_table;
uint64_t paging_arena_end = 0; 
uint64_t kernel_cr3;

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile ("wrmsr"
        :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

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
        // if (desc->Type == 7 || desc->Type == 1 || desc->Type == 2)
        if (desc->Type == EFI_CONVENTIONAL_MEMORY)
            if (end > ram_top) ram_top = end;
    }

    uint64_t fb_end = (uint64_t)bootInfo->framebuffer +
                      bootInfo->height * bootInfo->pixels_per_scanline * 4;
    uint64_t max_memory = ram_top;
    if (fb_end > max_memory) max_memory = fb_end;

    // Keep identity map large enough for common MMIO windows (LAPIC/IOAPIC).
    // LAPIC=0xFEE00000 and IOAPIC=0xFEC00000 are below 4 GiB.
    if (max_memory < IDENTITY_MIN_4G)
        max_memory = IDENTITY_MIN_4G;

    max_memory = (max_memory + PSE_2MB - 1) & ~(uint64_t)(PSE_2MB - 1);

    uint64_t addr = 0;
    for (uint64_t pdpt_i = 0; addr < max_memory && pdpt_i < ENTRIES; pdpt_i++) {
        uint64_t* pd = alloc_table();
        // pdpt[pdpt_i] = (uint64_t)pd | 0x3;
        pdpt[pdpt_i] = (uint64_t)pd | PAGE_PRESENT | PAGE_WRITE;
        for (uint64_t pd_i = 0; pd_i < ENTRIES && addr < max_memory; pd_i++) {
            uint64_t page_flags = PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE | PAGE_NX;
            if (addr == 0xFEC00000ULL || addr == 0xFEE00000ULL)
                page_flags |= PAGE_PCD;
            pd[pd_i] = addr | page_flags;
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

    uint64_t efer = rdmsr(0xC0000080);
    efer |= (1ULL << 11);  // NXE bit
    wrmsr(0xC0000080, efer);

    // verify
    uint64_t efer_check = rdmsr(0xC0000080);
    print("EFER after NXE: "); print_hex(efer_check); print("\n"); display_flush();

    __asm__ volatile ("cli");
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4) : "memory");

    // store kernel CR3 at physical 0x500 — always accessible
    // regardless of which address space is active
    // *((volatile uint64_t*)0x500) = (uint64_t)pml4;
    kernel_cr3 = (uint64_t)pml4;
    paging_arena_end = next_free_table;
    print("paging_arena_end = "); print_hex(paging_arena_end); print("\n");
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
