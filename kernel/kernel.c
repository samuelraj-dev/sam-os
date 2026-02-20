// #include "font8x8_basic.h"
#include "display.h"
#include "bootinfo.h"
#include "gdt.h"
#include "tss.h"
#include "idt.h"
#include "pic.h"
#include "timer.h"
#include "keyboard.h"
#include "paging.h"
#include "pmm.h"
#include "vmm.h"
#include "percpu.h"
#include "syscall.h"
#include "process.h"

#include "types.h"

static uint32_t* fb;
static uint32_t  screen_w, screen_h, pitch;
static uint32_t  cursor_x = 0;
static uint32_t  cursor_y = 0;
static uint32_t  fg_color = 0x00FFFFFF;
static uint32_t  bg_color = 0x00303030;

// ==================== BASIC IO PORT ====================
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

uint8_t bcd_to_bin(uint8_t val) {
    return (val & 0x0F) + ((val / 16) * 10);
}

void print_time() {
    uint8_t sec = bcd_to_bin(cmos_read(0x00));
    uint8_t min = bcd_to_bin(cmos_read(0x02));
    uint8_t hour = bcd_to_bin(cmos_read(0x04));
    uint8_t day = bcd_to_bin(cmos_read(0x07));
    uint8_t month = bcd_to_bin(cmos_read(0x08));
    uint8_t year = bcd_to_bin(cmos_read(0x09));

    print("Date: ");
    print_dec(day); print("/");
    print_dec(month); print("/");
    print_dec(2000 + year);
    print("\nTime: ");
    print_dec(hour); print(":");
    print_dec(min); print(":");
    print_dec(sec);
    print("\n");
}

void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    __asm__ volatile ("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf));
}

void print_cpu_info() {
    uint32_t a,b,c,d;
    char vendor[13];

    cpuid(0, &a, &b, &c, &d);

    *((uint32_t*)&vendor[0]) = b;
    *((uint32_t*)&vendor[4]) = d;
    *((uint32_t*)&vendor[8]) = c;
    vendor[12] = 0;

    print("CPU Vendor: ");
    print(vendor);
    print("\n");

    char brand[49];
    for (int i = 0; i < 3; i++) {
        cpuid(0x80000002 + i, &a, &b, &c, &d);
        *((uint32_t*)&brand[i*16 + 0]) = a;
        *((uint32_t*)&brand[i*16 + 4]) = b;
        *((uint32_t*)&brand[i*16 + 8]) = c;
        *((uint32_t*)&brand[i*16 +12]) = d;
    }
    brand[48] = 0;

    print("CPU: ");
    print(brand);
    print("\n");
}

void print_memory_map(BootInfo* bootInfo) {
    print("Memory Map Entries:\n");

    uint8_t* mmap = (uint8_t*)bootInfo->memory_map;

    for (uint64_t offset = 0;
         offset < bootInfo->memory_map_size;
         offset += bootInfo->memory_map_descriptor_size) {

        uint64_t type = *(uint32_t*)(mmap + offset);
        uint64_t phys = *(uint64_t*)(mmap + offset + 8);
        uint64_t pages = *(uint64_t*)(mmap + offset + 16);

        print("Type ");
        print_dec(type);
        print(" | Addr ");
        print_hex(phys);
        print(" | Pages ");
        print_dec(pages);
        print("\n");
    }
}

void print_total_ram(BootInfo* bootInfo)
{
    uint8_t* mmap = (uint8_t*)bootInfo->memory_map;
    uint64_t total_bytes = 0;

    for (uint64_t offset = 0;
         offset < bootInfo->memory_map_size;
         offset += bootInfo->memory_map_descriptor_size) {

        EFI_MEMORY_DESCRIPTOR* desc =
            (EFI_MEMORY_DESCRIPTOR*)(mmap + offset);

        if (desc->Type == 7) {  // EfiConventionalMemory
            total_bytes += desc->NumberOfPages * 4096ULL;
        }
    }

    uint64_t total_mb = total_bytes / (1024 * 1024);

    print("Usable RAM: ");
    print_dec(total_mb);
    print(" MB\n");
}

void debug_memory_map(BootInfo* bootInfo)
{
    uint8_t* mmap = (uint8_t*)bootInfo->memory_map;

    print("---- Memory Map Debug ----\n");

    for (uint64_t offset = 0;
         offset < bootInfo->memory_map_size;
         offset += bootInfo->memory_map_descriptor_size) {

        uint32_t type = *(uint32_t*)(mmap + offset);

        print("Type: ");
        print_dec(type);
        print("\n");
    }

    print("--------------------------\n");
}

void kernel_main(BootInfo* bootInfo)
{

    display_init(
        (uint32_t*)bootInfo->framebuffer,
        bootInfo->width,
        bootInfo->height,
        bootInfo->pixels_per_scanline
    );

    fb = (uint32_t*)bootInfo->framebuffer;
    screen_w = bootInfo->width;
    screen_h = bootInfo->height;
    pitch = bootInfo->pixels_per_scanline;

    // clear_screen();

    uint32_t* real_fb = (uint32_t*)bootInfo->framebuffer;
    for (uint32_t y = 0; y < bootInfo->height; y++)
        for (uint32_t x = 0; x < bootInfo->pixels_per_scanline; x++)
            real_fb[y * bootInfo->pixels_per_scanline + x] = 0x00303030;

    print("SamOS Kernel\n\n");


    print_total_ram(bootInfo);
    print("\n");

    print("Memory map size: ");
    print_dec(bootInfo->memory_map_size);
    print("\n");

    print("Descriptor size: ");
    print_dec(bootInfo->memory_map_descriptor_size);
    print("\n\n");

    uint8_t* raw = (uint8_t*)bootInfo->memory_map;
    print("raw bytes: ");
    for (int i = 0; i < 48; i++) {
        print_hex(raw[i]);
        print(" ");
    }
    print("\n");

    // print("Resolution: ");
    // print_dec(screen_w);
    // print("x");
    // print_dec(screen_h);
    // print("\n");

    print("Framebuffer: ");
    print_hex((uint64_t)bootInfo->framebuffer);
    print("\n\n");

    // print_cpu_info();
    // print("\n");

    // print_time();
    // print("\n");

    print("kernel start: ");
    print_hex((uint64_t)&_kernel_start);  // note: address-of, not value-of
    print("\n");

    print("kernel end: ");
    print_hex((uint64_t)&_kernel_end);
    print("\n");

    gdt_init();
    print("\nInitialized Global Descriptor Table (GDT).\n\n");
    tss_init();
    print("\nInitialized Task State Segment (TSS).\n\n");
    idt_init();
    print("\nInitialized Interrupt Descriptor Table (IDT).\n\n");
    pic_init();
    print("\nInitialized Programmable Interrupt Controller (PIC).\n\n");
    timer_init(100);
    keyboard_init();

    pic_unmask(0);
    pic_unmask(1);
    
    // __asm__ volatile ("sti");

    paging_init(bootInfo);
    print("\nPaging switched successfully.\n\n");
    gdt_init();
    tss_init();

    pmm_init(bootInfo);
    print("\nInitialized Physical Memory Manager (PMM).\n\n");

    // allocate shadow buffer — 1920*1080*4 = 8MB = 2048 pages
    uint32_t fb_bytes = display_get_height() * display_get_pitch() * 4;
    uint32_t fb_pages = (fb_bytes + 4095) / 4096;
    uint32_t* shadow = (uint32_t*)pmm_alloc_pages(fb_pages);
    if (!shadow) {
        print("ERROR: shadow alloc failed\n");
        while(1);
    }
    display_set_shadow(shadow);

    // clear shadow to match real fb
    for (uint32_t i = 0; i < display_get_height() * display_get_pitch(); i++)
        shadow[i] = 0x00303030;

    display_flush();

    // after pmm_init and shadow buffer setup, before sti:
    vmm_init();

    // test — create an address space and map a page
    AddressSpace* test_as = vmm_create_address_space();
    void* phys_page = pmm_alloc_page();
    vmm_map(test_as, 0x400000, (uint64_t)phys_page,
            VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);

    // verify mapping
    uint64_t resolved = vmm_virt_to_phys(test_as, 0x400000);
    print("VMM test — mapped 0x400000 to phys: ");
    print_hex(resolved);
    print("\n");

    // cleanup test
    vmm_unmap(test_as, 0x400000);
    pmm_free_page(phys_page);
    vmm_destroy_address_space(test_as);
    print("VMM test passed\n");

    percpu_init();
    syscall_init();

    __asm__ volatile ("sti");

    // void* p1 = pmm_alloc_page();
    // void* p2 = pmm_alloc_page();
    // print("alloc1: "); print_hex((uint64_t)p1); print("\n");
    // print("alloc2: "); print_hex((uint64_t)p2); print("\n");
    // pmm_free_page(p1);
    // void* p3 = pmm_alloc_page();  // should return same as p1
    // print("alloc3: "); print_hex((uint64_t)p3); print("\n");

    // these symbols come from hello_blob.o
    extern uint8_t _binary_user_hello_bin_start[];
    extern uint8_t _binary_user_hello_bin_end[];

    uint64_t hello_size = _binary_user_hello_bin_end
                        - _binary_user_hello_bin_start;

    print("Launching user process...\n");
    Process* proc = process_create_from_elf(
        _binary_user_hello_bin_start, hello_size);

    if (proc) {
        process_run(proc);  // does not return
    } else {
        print("Failed to create process\n");
    }

    print("SamOS ready type something:\n");

    uint64_t last = 0;
    while (1) {
        __asm__ volatile ("hlt");

        // poll keyboard status directly
        if (inb(0x64) & 0x01) {
            uint8_t sc = inb(0x60);
            if (sc == 0x00 || sc == 0xAA || sc == 0xFA) continue; // filter init noise
            if (!(sc & 0x80) && sc < 128 && scancode_map[sc]) {
                char str[2] = { scancode_map[sc], 0 };
                print(str);
            }
        }

        if (timer_flush_needed()) display_flush();       // flush in main loop, not in IRQ

        uint64_t t = timer_get_ticks();
        if (t >= last + 100) {
            last = t;  // snap to current, don't accumulate drift
            print("tick: ");
            print_dec(t);
            print("\n");
        }
    }

    // int test_x = 1/0;
    // *(volatile uint64_t*)0xdeadbeef = 1;
    // *(volatile int*)0 = 0;
    
    // debug_memory_map(bootInfo);


    // pmm_init(bootInfo);
    // print("Free pages: ");
    // print_dec(pmm_get_free_pages());
    // print("\n");

    // void* test = pmm_alloc_page();

    // print("Allocated page: ");
    // print_hex((uint64_t)test);
    // print("\n");

    // print("\nPMM initialized\n");

    // while (1)
    //     __asm__ volatile ("hlt");
}