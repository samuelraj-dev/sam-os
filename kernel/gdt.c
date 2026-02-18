#include "gdt.h"
#include "display.h"

static GDTEntry gdt[7];
static GDTDescriptor gdtr;

// access byte breakdown:
//   bit 7   : Present (must be 1 for valid segment)
//   bits 6-5: DPL — privilege level (0=kernel, 3=user)
//   bit 4   : Descriptor type (1=code/data, 0=system)
//   bit 3   : Executable (1=code, 0=data)
//   bit 2   : Direction/Conforming (0 for both)
//   bit 1   : Read/Write
//   bit 0   : Accessed (CPU sets this, leave 0)

// granularity byte breakdown:
//   bit 7: Granularity (0=byte, 1=4KB) — ignored in 64-bit
//   bit 6: Size (0=16-bit, 1=32-bit)   — must be 0 in 64-bit
//   bit 5: Long mode (1=64-bit code)   — key flag
//   bit 4: Reserved

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid1;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_mid2;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed)) TSSDescriptor;

void gdt_set_tss(uint64_t tss_addr, uint32_t tss_size)
{
    TSSDescriptor* desc = (TSSDescriptor*)&gdt[5];

    desc->limit_low  = tss_size & 0xFFFF;
    desc->base_low   = tss_addr & 0xFFFF;
    desc->base_mid1  = (tss_addr >> 16) & 0xFF;
    desc->access     = 0x89;  // present, ring 0, 64-bit TSS available
    desc->granularity = 0x00;
    desc->base_mid2  = (tss_addr >> 24) & 0xFF;
    desc->base_high  = (tss_addr >> 32) & 0xFFFFFFFF;
    desc->reserved   = 0;
}

static void set_entry(int i, uint8_t access, uint8_t granularity)
{
    gdt[i].limit_low  = 0xFFFF;
    gdt[i].base_low   = 0;
    gdt[i].base_mid   = 0;
    gdt[i].access     = access;
    gdt[i].granularity = granularity;
    gdt[i].base_high  = 0;
}

void gdt_init(void)
{
    // Null descriptor
    gdt[0] = (GDTEntry){0};

    // Kernel code: present, ring 0, code, readable, 64-bit
    set_entry(1, 0x9A, 0x20);
    //           ^^^^  ^^^^
    //           |     bit 5 set = 64-bit long mode
    //           present(1) + DPL=00 + type=1 + exec=1 + rw=1

    // Kernel data: present, ring 0, data, writable
    set_entry(2, 0x92, 0x00);
    //           ^^^^
    //           present(1) + DPL=00 + type=1 + exec=0 + rw=1

    // User code: present, ring 3, code, readable, 64-bit
    set_entry(3, 0xFA, 0x20);
    //           ^^^^
    //           present(1) + DPL=11 + type=1 + exec=1 + rw=1

    // User data: present, ring 3, data, writable
    set_entry(4, 0xF2, 0x00);
    //           ^^^^
    //           present(1) + DPL=11 + type=1 + exec=0 + rw=1

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint64_t)&gdt;

    // Load GDTR and reload segment registers
    __asm__ volatile (
        "lgdt %0\n"

        // Reload data segments with kernel data selector
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"

        // Reload CS via far return — can't mov directly into CS
        // push new CS, push return address, retfq jumps to it
        "pushq $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        :
        : "m"(gdtr)
        : "rax", "memory"
    );

    print("GDT loaded\n");
}
