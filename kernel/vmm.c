#include "vmm.h"
#include "pmm.h"
#include "display.h"
#include "paging.h"

AddressSpace kernel_address_space;

// get current CR3
static uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

// flush TLB for a single page
static void tlb_flush(uint64_t virt) {
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

// allocate a zeroed physical page for a page table
static uint64_t* alloc_page_table(void) {
    uint64_t* pt = (uint64_t*)pmm_alloc_page();
    if (!pt) {
        print("VMM: out of memory\n");
        while(1);
    }
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
    if (table[index] & VMM_FLAG_PRESENT) {
        // already exists — return pointer to it
        return (uint64_t*)(table[index] & ~0xFFFULL);
    }

    // allocate new table
    uint64_t* new_table = alloc_page_table();
    table[index] = (uint64_t)new_table | flags | VMM_FLAG_PRESENT;
    return new_table;
}

void vmm_init(void)
{
    // wrap the existing kernel page tables into an AddressSpace
    kernel_address_space.pml4 = (uint64_t*)read_cr3();
    print("VMM: initialized\n");
    print("VMM: kernel PML4 at: ");
    print_hex((uint64_t)kernel_address_space.pml4);
    print("\n");
}

AddressSpace* vmm_create_address_space(void)
{
    AddressSpace* as = (AddressSpace*)pmm_alloc_page();
    if (!as) return 0;

    as->pml4 = alloc_page_table();

    // zero all user entries
    for (int i = 0; i < 512; i++)
        as->pml4[i] = 0;

    // share kernel mapping — copy PML4[511] from kernel
    as->pml4[511] = kernel_address_space.pml4[511];

    return as;
}

void vmm_destroy_address_space(AddressSpace* as)
{
    // free all user page tables (indices 0-510)
    // don't free [511] — that's shared kernel mapping
    for (int pml4_i = 0; pml4_i < 511; pml4_i++) {
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
    uint64_t* pt   = get_or_create_table(pd,       pd_i,   table_flags);

    // set the final page table entry
    pt[pt_i] = (phys & ~0xFFFULL) | flags | VMM_FLAG_PRESENT;

    tlb_flush(virt);
}

void vmm_unmap(AddressSpace* as, uint64_t virt)
{
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

uint64_t vmm_virt_to_phys(AddressSpace* as, uint64_t virt)
{
    uint16_t pml4_i = (virt >> 39) & 0x1FF;
    uint16_t pdpt_i = (virt >> 30) & 0x1FF;
    uint16_t pd_i   = (virt >> 21) & 0x1FF;
    uint16_t pt_i   = (virt >> 12) & 0x1FF;

    if (!(as->pml4[pml4_i] & VMM_FLAG_PRESENT)) return 0;
    uint64_t* pdpt = (uint64_t*)(as->pml4[pml4_i] & ~0xFFFULL);

    if (!(pdpt[pdpt_i] & VMM_FLAG_PRESENT)) return 0;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_i] & ~0xFFFULL);

    if (!(pd[pd_i] & VMM_FLAG_PRESENT)) return 0;
    uint64_t* pt = (uint64_t*)(pd[pd_i] & ~0xFFFULL);

    if (!(pt[pt_i] & VMM_FLAG_PRESENT)) return 0;
    return (pt[pt_i] & ~0xFFFULL) | (virt & 0xFFFULL);
}