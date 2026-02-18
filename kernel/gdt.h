#ifndef GDT_H
#define GDT_H

#include "types.h"

// Segment selectors — index * 8, | 3 for user segments (sets RPL=3)
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER_CODE    0x18
#define GDT_USER_DATA    0x20
#define GDT_TSS  0x28   // index 5, takes slots 5+6

typedef struct {
    uint16_t limit_low;    // ignored in 64-bit
    uint16_t base_low;     // ignored in 64-bit
    uint8_t  base_mid;     // ignored in 64-bit
    uint8_t  access;       // privilege, type flags
    uint8_t  granularity;  // 64-bit flag lives here
    uint8_t  base_high;    // ignored in 64-bit
} __attribute__((packed)) GDTEntry;

typedef struct {
    uint16_t limit;   // size of GDT - 1
    uint64_t base;    // address of GDT
} __attribute__((packed)) GDTDescriptor;

void gdt_set_tss(uint64_t tss_addr, uint32_t tss_size);
void gdt_init(void);

#endif