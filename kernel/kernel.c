// typedef unsigned char uint8_t;
// typedef unsigned short uint16_t;
// typedef unsigned int uint32_t;
// typedef unsigned long long uint64_t;

// typedef struct {
//     void* framebuffer;
//     uint32_t width;
//     uint32_t height;
//     uint32_t pixels_per_scanline;
//     void* memory_map;
//     uint64_t memory_map_size;
//     uint64_t memory_map_descriptor_size;
// } BootInfo;

// void kernel_main(BootInfo* bootInfo)
// {
//     if (!bootInfo || !bootInfo->framebuffer)
//         goto halt;

//     uint32_t* fb = (uint32_t*)bootInfo->framebuffer;
//     uint32_t width = bootInfo->width;
//     uint32_t height = bootInfo->height;
//     uint32_t pitch = bootInfo->pixels_per_scanline;

//     // Fill screen with dark gray
//     for (uint32_t y = 0; y < height; y++) {
//         for (uint32_t x = 0; x < width; x++) {
//             fb[y * pitch + x] = 0x00303030;
//         }
//     }

//     // Draw red rectangle in center
//     uint32_t rect_w = 300;
//     uint32_t rect_h = 200;
//     uint32_t start_x = (width - rect_w) / 2;
//     uint32_t start_y = (height - rect_h) / 2;

//     for (uint32_t y = 0; y < rect_h; y++) {
//         for (uint32_t x = 0; x < rect_w; x++) {
//             fb[(start_y + y) * pitch + (start_x + x)] = 0x00FF0000;
//         }
//     }

// halt:
//     while (1) {
//         __asm__ volatile ("hlt");
//     }
// }
#include "font8x8_basic.h"
#include "bootinfo.h"
#include "types.h"

#include "paging.h"
#include "pmm.h"

// ==================== BASIC IO PORT ====================
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

// uint8_t font8x8_basic[128][8];

static uint32_t* fb;
static uint32_t screen_w, screen_h, pitch;
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint32_t fg_color = 0x00FFFFFF;
static uint32_t bg_color = 0x00303030;

void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    fb[y * pitch + x] = color;
}

void draw_char(char c, uint32_t x, uint32_t y) {
    // uint8_t* glyph = font8x8_basic[(uint8_t)c];
    unsigned char* glyph = font8x8_basic[(uint8_t)c];

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (glyph[row] & (1 << col))
                put_pixel(x + col, y + row, fg_color);
            else
                put_pixel(x + col, y + row, bg_color);
        }
    }
}

void print_char(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y += 10;
        return;
    }

    draw_char(c, cursor_x, cursor_y);
    cursor_x += 8;

    if (cursor_x + 8 >= screen_w) {
        cursor_x = 0;
        cursor_y += 10;
    }
}

void print(const char* str) {
    while (*str)
        print_char(*str++);
}

void print_hex(uint64_t val) {
    char hex[] = "0123456789ABCDEF";
    print("0x");
    for (int i = 60; i >= 0; i -= 4)
        print_char(hex[(val >> i) & 0xF]);
}

void print_dec(uint64_t val) {
    char buf[32];
    int i = 0;

    if (val == 0) {
        print("0");
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    for (int j = i - 1; j >= 0; j--)
        print_char(buf[j]);
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
    
    fb = (uint32_t*)bootInfo->framebuffer;
    screen_w = bootInfo->width;
    screen_h = bootInfo->height;
    pitch = bootInfo->pixels_per_scanline;

    // Clear screen
    for (uint32_t y = 0; y < screen_h; y++)
        for (uint32_t x = 0; x < screen_w; x++)
            fb[y * pitch + x] = bg_color;

    print("SamOS Kernel\n\n");

    print("Memory map size: ");
    print_dec(bootInfo->memory_map_size);
    print("\n");

    print("Descriptor size: ");
    print_dec(bootInfo->memory_map_descriptor_size);
    print("\n\n");

    print("Resolution: ");
    print_dec(screen_w);
    print("x");
    print_dec(screen_h);
    print("\n");

    print("Framebuffer: ");
    print_hex((uint64_t)bootInfo->framebuffer);
    print("\n\n");

    print_cpu_info();
    print("\n");

    print_time();
    print("\n");

    paging_init(bootInfo);
    
    print("\nPaging switched successfully.\n\n");
    
    print_total_ram(bootInfo);
    // debug_memory_map(bootInfo);


    // pmm_init(bootInfo);

    // print("\nPMM initialized\n");

    while (1)
        __asm__ volatile ("hlt");
}