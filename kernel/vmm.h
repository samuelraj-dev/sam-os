#ifndef VMM_H
#define VMM_H

#include "types.h"

#define PAGE_SIZE      4096
#define VMM_FLAG_PRESENT   (1ULL << 0)
#define VMM_FLAG_WRITE     (1ULL << 1)
#define VMM_FLAG_USER      (1ULL << 2)
#define VMM_FLAG_NX        (1ULL << 63)

// an address space
typedef struct {
    uint64_t* pml4;        // physical address of PML4
} AddressSpace;

void          vmm_init(void);
AddressSpace* vmm_create_address_space(void);
void          vmm_destroy_address_space(AddressSpace* as);
void          vmm_map(AddressSpace* as, uint64_t virt,
                      uint64_t phys, uint64_t flags);
void          vmm_unmap(AddressSpace* as, uint64_t virt);
void          vmm_switch(AddressSpace* as);
uint64_t      vmm_virt_to_phys(AddressSpace* as, uint64_t virt);

// kernel address space — shared by all
extern AddressSpace kernel_address_space;

#endif