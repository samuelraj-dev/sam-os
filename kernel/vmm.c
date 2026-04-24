#include "vmm.h"
#include "pmm.h"
#include "display.h"
#include "paging.h"
#include "panic.h"

AddressSpace kernel_address_space;

// flush TLB for a single page
static void tlb_flush(uint64_t virt) {
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

// allocate a zeroed physical page for a page table
static uint64_t* alloc_page_table(void) {
    uint64_t* pt = (uint64_t*)pmm_alloc_page();
    if (!pt)
        panic("VMM: out of memory allocating page table");
    // zero it — pt is a physical address, accessible via identity map
    for (int i = 0; i < 512; i++) pt[i] = 0;
    return pt;
}

// get or create a page table entry at a given level
// returns pointer to the next level table
static uint64_t* get_or_create_table(uint64_t* table,
                                      uint16_t index,
                                      uint64_t flags)
{
    if (table[index] & VMM_FLAG_PRESENT)
        return (uint64_t*)(table[index] & 0x000FFFFFFFFFF000ULL);  // fix here

    uint64_t* new_table = alloc_page_table();
    table[index] = (uint64_t)new_table | flags | VMM_FLAG_PRESENT;
    return new_table;
}

// void vmm_init(void)
// {
//     // wrap the existing kernel page tables into an AddressSpace
//     kernel_address_space.pml4 = (uint64_t*)read_cr3();
//     print("VMM: initialized\n");
//     print("VMM: kernel PML4 at: ");
//     print_hex((uint64_t)kernel_address_space.pml4);
//     print("\n");
// }
void vmm_init(void)
{
    // read current CR3 — this is the kernel PML4 built by paging_init
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    kernel_address_space.pml4 = (uint64_t*)cr3;

    print("VMM: initialized\n");
    print("VMM: kernel PML4 at: ");
    print_hex((uint64_t)kernel_address_space.pml4);
    print("\n");
}

// AddressSpace* vmm_create_address_space(void)
// {
//     AddressSpace* as = (AddressSpace*)pmm_alloc_page();
//     if (!as) return 0;

//     as->pml4 = alloc_page_table();

//     for (int i = 0; i < 512; i++)
//         as->pml4[i] = 0;

//     // share kernel high mapping
//     as->pml4[511] = kernel_address_space.pml4[511];

//     // also copy identity map (pml4[0]) so kernel can access
//     // physical memory (framebuffer, page tables) after switching
//     as->pml4[0] = kernel_address_space.pml4[0];

//     return as;
// }
AddressSpace* vmm_create_address_space(void)
{
    AddressSpace* as = (AddressSpace*)pmm_alloc_page();
    if (!as) return 0;

    as->pml4 = alloc_page_table();

    for (int i = 0; i < 512; i++)
        as->pml4[i] = 0;

    // Copy identity map (pml4[0]) so kernel can access physical memory
    // (framebuffer, page tables, bootinfo, etc.) after switching CR3
    as->pml4[0] = kernel_address_space.pml4[0];

    // Share kernel high mapping (pml4[511]) for kernel code/data
    as->pml4[511] = kernel_address_space.pml4[511];

    return as;
}

void vmm_destroy_address_space(AddressSpace* as)
{
    // pml4[0] and pml4[511] are shared with the kernel during this stage.
    for (int pml4_i = 1; pml4_i < 511; pml4_i++) {
        if (!(as->pml4[pml4_i] & VMM_FLAG_PRESENT)) continue;

        uint64_t* pdpt = (uint64_t*)(as->pml4[pml4_i] & ~0xFFFULL);
        for (int pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            if (!(pdpt[pdpt_i] & VMM_FLAG_PRESENT)) continue;

            uint64_t* pd = (uint64_t*)(pdpt[pdpt_i] & ~0xFFFULL);
            for (int pd_i = 0; pd_i < 512; pd_i++) {
                if (!(pd[pd_i] & VMM_FLAG_PRESENT)) continue;

                // free the page table
                uint64_t* pt = (uint64_t*)(pd[pd_i] & ~0xFFFULL);
                pmm_free_page(pt);
            }
            pmm_free_page(pd);
        }
        pmm_free_page(pdpt);
    }

    pmm_free_page(as->pml4);
    pmm_free_page(as);
}

void vmm_map(AddressSpace* as, uint64_t virt,
             uint64_t phys, uint64_t flags)
{
    kassert(as != 0, "vmm_map: null address space");
    kassert((virt & 0xFFFULL) == 0, "vmm_map: virtual address is not page aligned");
    kassert((phys & 0xFFFULL) == 0, "vmm_map: physical address is not page aligned");

    // extract indices from virtual address
    uint16_t pml4_i = (virt >> 39) & 0x1FF;
    uint16_t pdpt_i = (virt >> 30) & 0x1FF;
    uint16_t pd_i   = (virt >> 21) & 0x1FF;
    uint16_t pt_i   = (virt >> 12) & 0x1FF;

    // walk/create page table hierarchy
    uint64_t table_flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITE |
                           (flags & VMM_FLAG_USER ? VMM_FLAG_USER : 0);

    uint64_t* pdpt = get_or_create_table(as->pml4, pml4_i, table_flags);
    uint64_t* pd   = get_or_create_table(pdpt,     pdpt_i, table_flags);
    kassert((pd[pd_i] & (1ULL << 7)) == 0, "vmm_map: cannot map through a huge page");
    uint64_t* pt   = get_or_create_table(pd,       pd_i,   table_flags);

    // set the final page table entry
    kassert((pt[pt_i] & VMM_FLAG_PRESENT) == 0, "vmm_map: virtual page is already mapped");
    pt[pt_i] = (phys & ~0xFFFULL) | flags | VMM_FLAG_PRESENT;

    tlb_flush(virt);
}

void vmm_unmap(AddressSpace* as, uint64_t virt)
{
    kassert(as != 0, "vmm_unmap: null address space");
    kassert((virt & 0xFFFULL) == 0, "vmm_unmap: virtual address is not page aligned");

    uint16_t pml4_i = (virt >> 39) & 0x1FF;
    uint16_t pdpt_i = (virt >> 30) & 0x1FF;
    uint16_t pd_i   = (virt >> 21) & 0x1FF;
    uint16_t pt_i   = (virt >> 12) & 0x1FF;

    if (!(as->pml4[pml4_i] & VMM_FLAG_PRESENT)) return;
    uint64_t* pdpt = (uint64_t*)(as->pml4[pml4_i] & ~0xFFFULL);

    if (!(pdpt[pdpt_i] & VMM_FLAG_PRESENT)) return;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_i] & ~0xFFFULL);

    if (!(pd[pd_i] & VMM_FLAG_PRESENT)) return;
    uint64_t* pt = (uint64_t*)(pd[pd_i] & ~0xFFFULL);

    pt[pt_i] = 0;
    tlb_flush(virt);
}

void vmm_switch(AddressSpace* as)
{
    __asm__ volatile ("mov %0, %%cr3" :: "r"(as->pml4) : "memory");
}

// Helper: convert physical address to kernel virtual address via identity map
// Only works if identity map (pml4[0]) is still active
static uint64_t phys_to_virt_identity(uint64_t phys) {
    // Identity map maps physical addresses directly to virtual addresses
    // So phys 0x1000 -> virt 0x1000
    return phys;
}

uint64_t vmm_virt_to_phys(AddressSpace* as, uint64_t virt)
{
    uint16_t pml4_i = (virt >> 39) & 0x1FF;
    uint16_t pdpt_i = (virt >> 30) & 0x1FF;
    uint16_t pd_i   = (virt >> 21) & 0x1FF;
    uint16_t pt_i   = (virt >> 12) & 0x1FF;

    if (!(as->pml4[pml4_i] & VMM_FLAG_PRESENT)) return 0;
    uint64_t pdpt_phys = as->pml4[pml4_i] & 0x000FFFFFFFFFF000ULL;
    uint64_t* pdpt = (uint64_t*)phys_to_virt_identity(pdpt_phys);
    
    if (!(pdpt[pdpt_i] & VMM_FLAG_PRESENT)) return 0;
    uint64_t pd_phys = pdpt[pdpt_i] & 0x000FFFFFFFFFF000ULL;
    uint64_t* pd = (uint64_t*)phys_to_virt_identity(pd_phys);

    if (!(pd[pd_i] & VMM_FLAG_PRESENT)) return 0;
    uint64_t pt_phys = pd[pd_i] & 0x000FFFFFFFFFF000ULL;
    uint64_t* pt = (uint64_t*)phys_to_virt_identity(pt_phys);

    if (!(pt[pt_i] & VMM_FLAG_PRESENT)) return 0;

    // mask out ALL flag bits — bits 63 (NX) and 11:0 (flags)
    return (pt[pt_i] & 0x000FFFFFFFFFF000ULL) | (virt & 0xFFFULL);
}
