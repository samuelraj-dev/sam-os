// #include "pmm.h"
// #include "display.h"

// #include "types.h"


// #define PAGE_SIZE 4096

// static uint8_t* bitmap = 0;
// static uint64_t total_pages = 0;
// static uint64_t free_pages = 0;
// static uint64_t bitmap_size = 0;

// /* ================= BITMAP HELPERS ================= */

// static inline void set_bit(uint64_t bit)
// {
//     bitmap[bit / 8] |= (1 << (bit % 8));
// }

// static inline void clear_bit(uint64_t bit)
// {
//     bitmap[bit / 8] &= ~(1 << (bit % 8));
// }

// static inline int test_bit(uint64_t bit)
// {
//     return bitmap[bit / 8] & (1 << (bit % 8));
// }

// /* ================= PMM INIT ================= */

// void pmm_init(BootInfo* bootInfo)
// {
//     uint8_t* mmap = (uint8_t*)bootInfo->memory_map;

//     uint64_t highest_usable = 0;

//     /* 1️⃣ Find highest usable RAM address */
//     for (uint64_t offset = 0;
//          offset < bootInfo->memory_map_size;
//          offset += bootInfo->memory_map_descriptor_size)
//     {
//         EFI_MEMORY_DESCRIPTOR* desc =
//             (EFI_MEMORY_DESCRIPTOR*)(mmap + offset);

//         if (desc->Type == 7)  // EfiConventionalMemory
//         {
//             uint64_t end =
//                 desc->PhysicalStart +
//                 desc->NumberOfPages * PAGE_SIZE;

//             if (end > highest_usable)
//                 highest_usable = end;
//         }
//     }

//     total_pages = highest_usable / PAGE_SIZE;
//     bitmap_size = total_pages / 8 + 1;

//     /* 2️⃣ Find usable region large enough for bitmap */
//     for (uint64_t offset = 0;
//          offset < bootInfo->memory_map_size;
//          offset += bootInfo->memory_map_descriptor_size)
//     {
//         EFI_MEMORY_DESCRIPTOR* desc =
//             (EFI_MEMORY_DESCRIPTOR*)(mmap + offset);

//         if (desc->Type == 7 &&
//             desc->NumberOfPages * PAGE_SIZE >= bitmap_size)
//         {
//             bitmap = (uint8_t*)desc->PhysicalStart;
//             break;
//         }
//     }

//     if (!bitmap)
//         while (1);  // fatal: no space for bitmap

//     /* 3️⃣ Mark all pages as used initially */
//     for (uint64_t i = 0; i < bitmap_size; i++)
//         bitmap[i] = 0xFF;

//     free_pages = 0;

//     /* 4️⃣ Mark usable pages free */
//     for (uint64_t offset = 0;
//          offset < bootInfo->memory_map_size;
//          offset += bootInfo->memory_map_descriptor_size)
//     {
//         EFI_MEMORY_DESCRIPTOR* desc =
//             (EFI_MEMORY_DESCRIPTOR*)(mmap + offset);

//         if (desc->Type == 7)
//         {
//             uint64_t start_page =
//                 desc->PhysicalStart / PAGE_SIZE;

//             for (uint64_t i = 0;
//                  i < desc->NumberOfPages;
//                  i++)
//             {
//                 clear_bit(start_page + i);
//                 free_pages++;
//             }
//         }
//     }

//     /* 5️⃣ Reserve bitmap pages */
//     uint64_t bitmap_pages =
//         (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;

//     uint64_t bitmap_start_page =
//         (uint64_t)bitmap / PAGE_SIZE;

//     for (uint64_t i = 0; i < bitmap_pages; i++)
//     {
//         if (!test_bit(bitmap_start_page + i))
//         {
//             set_bit(bitmap_start_page + i);
//             free_pages--;
//         }
//     }

//     /* 6️⃣ Reserve kernel pages using linker symbols */
//     uint64_t kernel_start =
//         (uint64_t)&_kernel_start;

//     uint64_t kernel_end =
//         (uint64_t)&_kernel_end;

//     uint64_t start_page =
//         kernel_start / PAGE_SIZE;

//     uint64_t end_page =
//         (kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;

//     for (uint64_t i = start_page; i < end_page; i++)
//     {
//         if (!test_bit(i))
//         {
//             set_bit(i);
//             free_pages--;
//         }
//     }
// }

// /* ================= ALLOC ================= */

// void* pmm_alloc_page()
// {
//     for (uint64_t i = 0; i < total_pages; i++)
//     {
//         if (!test_bit(i))
//         {
//             set_bit(i);
//             free_pages--;
//             return (void*)(i * PAGE_SIZE);
//         }
//     }

//     return 0;  // out of memory
// }

// /* ================= FREE ================= */

// void pmm_free_page(void* addr)
// {
//     uint64_t page =
//         (uint64_t)addr / PAGE_SIZE;

//     if (test_bit(page))
//     {
//         clear_bit(page);
//         free_pages++;
//     }
// }

// uint64_t pmm_get_free_pages()
// {
//     return free_pages;
// }

#include "pmm.h"
#include "paging.h"
#include "display.h"

// placed right after page tables by paging
// we'll get this address passed in or use a fixed region
static uint8_t* bitmap = 0;
static uint64_t bitmap_size   = 0;   // in bytes
static uint64_t total_pages   = 0;
static uint64_t free_pages    = 0;

// ── bitmap helpers ──────────────────────────────────────
static void bitmap_set(uint64_t page_index) {
    bitmap[page_index / 8] |= (1 << (page_index % 8));
}

static void bitmap_clear(uint64_t page_index) {
    bitmap[page_index / 8] &= ~(1 << (page_index % 8));
}

static int bitmap_test(uint64_t page_index) {
    return bitmap[page_index / 8] & (1 << (page_index % 8));
}

void pmm_init(BootInfo* bootInfo) {
    uint8_t*  mmap  = (uint8_t*)bootInfo->memory_map;
    uint64_t  dsize = bootInfo->memory_map_descriptor_size;

    uint64_t max_addr = 0;
    for (uint64_t off = 0; off < bootInfo->memory_map_size; off += dsize) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)(mmap + off);
        uint64_t end = desc->PhysicalStart + desc->NumberOfPages * PAGE_SIZE;
        if (end > max_addr) max_addr = end;
    }

    total_pages = max_addr / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8;

    // place bitmap after page tables — paging_arena_end is physical
    extern uint64_t paging_arena_end;
    bitmap = (uint8_t*)paging_arena_end;

    // mark everything used
    for (uint64_t i = 0; i < bitmap_size; i++)
        bitmap[i] = 0xFF;
    free_pages = 0;

    // free conventional memory
    for (uint64_t off = 0; off < bootInfo->memory_map_size; off += dsize) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)(mmap + off);
        if (desc->Type != 7) continue;
        for (uint64_t p = 0; p < desc->NumberOfPages; p++) {
            uint64_t page = (desc->PhysicalStart / PAGE_SIZE) + p;
            bitmap_clear(page);
            free_pages++;
        }
    }

    // mark kernel used
    uint64_t kernel_end_virt = (uint64_t)&_kernel_end;
    uint64_t kernel_end_phys = kernel_end_virt - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE;
    uint64_t k_start = KERNEL_PHYS_BASE / PAGE_SIZE;
    uint64_t k_end   = (kernel_end_phys + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t p = k_start; p < k_end; p++)
        if (!bitmap_test(p)) { bitmap_set(p); free_pages--; }

    // mark page tables used
    uint64_t pt_start = k_end;
    uint64_t pt_end   = paging_arena_end / PAGE_SIZE;
    for (uint64_t p = pt_start; p < pt_end; p++)
        if (!bitmap_test(p)) { bitmap_set(p); free_pages--; }

    // mark bitmap itself used
    uint64_t bm_start = paging_arena_end / PAGE_SIZE;
    uint64_t bm_end   = (paging_arena_end + bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t p = bm_start; p < bm_end; p++)
        if (!bitmap_test(p)) { bitmap_set(p); free_pages--; }

    // never allocate page 0
    if (!bitmap_test(0)) { bitmap_set(0); free_pages--; }

    print("PMM: free RAM: ");
    print_dec(free_pages * PAGE_SIZE / (1024 * 1024));
    print(" MB\n");
}

// ── alloc ────────────────────────────────────────────────
void* pmm_alloc_page(void) {
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_pages--;
            return (void*)(i * PAGE_SIZE);
        }
    }
    return 0;  // out of memory
}

// pmm.c
void* pmm_alloc_pages(uint64_t count)
{
    for (uint64_t i = 1; i < total_pages - count; i++) {
        int found = 1;
        for (uint64_t j = i; j < i + count; j++) {
            if (bitmap_test(j)) { 
                i = j;  // skip ahead to after the used page
                found = 0; 
                break; 
            }
        }
        if (found) {
            for (uint64_t j = i; j < i + count; j++) {
                bitmap_set(j);
                free_pages--;
            }
            return (void*)(i * PAGE_SIZE);
        }
    }
    return 0;
}

// ── free ─────────────────────────────────────────────────
void pmm_free_page(void* addr) {
    uint64_t page = (uint64_t)addr / PAGE_SIZE;
    if (page == 0 || page >= total_pages) return;  // guard
    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_pages++;
    }
    // silently ignore double-free
}

// ── stats ────────────────────────────────────────────────
uint64_t pmm_get_free_pages(void)  { return free_pages; }
uint64_t pmm_get_total_pages(void) { return total_pages; }