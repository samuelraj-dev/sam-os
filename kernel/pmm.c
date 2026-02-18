#include "pmm.h"
#include "types.h"

#define PAGE_SIZE 4096

static uint8_t* bitmap = 0;
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static uint64_t bitmap_size = 0;

static inline void set_bit(uint64_t bit) {
    bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void clear_bit(uint64_t bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline int test_bit(uint64_t bit) {
    return bitmap[bit / 8] & (1 << (bit % 8));
}

void pmm_init(BootInfo* bootInfo)
{
    uint8_t* mmap = (uint8_t*)bootInfo->memory_map;

    uint64_t max_addr = 0;

    for (uint64_t offset = 0;
         offset < bootInfo->memory_map_size;
         offset += bootInfo->memory_map_descriptor_size) {

        uint64_t type  = *(uint32_t*)(mmap + offset);
        uint64_t phys  = *(uint64_t*)(mmap + offset + 8);
        uint64_t pages = *(uint64_t*)(mmap + offset + 16);

        uint64_t end = phys + pages * PAGE_SIZE;
        if (end > max_addr)
            max_addr = end;
    }

    total_pages = max_addr / PAGE_SIZE;
    bitmap_size = total_pages / 8 + 1;

    // for (uint64_t offset = 0;
    //     offset < bootInfo->memory_map_size;
    //     offset += bootInfo->memory_map_descriptor_size) {

    //     uint64_t type  = *(uint32_t*)(mmap + offset);
    //     uint64_t phys  = *(uint64_t*)(mmap + offset + 8);
    //     uint64_t pages = *(uint64_t*)(mmap + offset + 16);

    //     // 7 = EfiConventionalMemory
    //     if (type == 7 && pages * PAGE_SIZE >= bitmap_size) {
    //         bitmap = (uint8_t*)phys;
    //         break;
    //     }
    // }
    bitmap = (uint8_t*)0x1000000;  // 16MB mark

    for (uint64_t i = 0; i < bitmap_size; i++)
        bitmap[i] = 0xFF;

        for (uint64_t offset = 0;
         offset < bootInfo->memory_map_size;
         offset += bootInfo->memory_map_descriptor_size) {

        uint64_t type  = *(uint32_t*)(mmap + offset);
        uint64_t phys  = *(uint64_t*)(mmap + offset + 8);
        uint64_t pages = *(uint64_t*)(mmap + offset + 16);

        if (type == 7) {  // EfiConventionalMemory
            uint64_t start_page = phys / PAGE_SIZE;

            for (uint64_t p = 0; p < pages; p++) {
                clear_bit(start_page + p);
                free_pages++;
            }
        }
    }
}

void* pmm_alloc_page()
{
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!test_bit(i)) {
            set_bit(i);
            free_pages--;
            return (void*)(i * PAGE_SIZE);
        }
    }

    return 0;
}

void pmm_free_page(void* addr)
{
    uint64_t page = (uint64_t)addr / PAGE_SIZE;

    if (test_bit(page)) {
        clear_bit(page);
        free_pages++;
    }
}