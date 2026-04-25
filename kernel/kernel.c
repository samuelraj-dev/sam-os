#include "display.h"
#include "bootinfo.h"
#include "gdt.h"
#include "tss.h"
#include "idt.h"
#include "irq.h"
#include "timer.h"
#include "keyboard.h"
#include "paging.h"
#include "pmm.h"
#include "vmm.h"
#include "panic.h"
#include "klog.h"
#include "kmalloc.h"
#include "acpi.h"
#include "rtc.h"
#include "scheduler/task.h"
#include "process.h"
#include "syscall_abi.h"
#include "types.h"

#define SCREEN_BG 0x00303030
#define TIMER_HZ  100

static void halt_forever(void)
{
    while (1)
        __asm__ volatile ("hlt");
}

static void clear_framebuffer(BootInfo* bootInfo)
{
    uint32_t* fb = (uint32_t*)bootInfo->framebuffer;

    for (uint32_t y = 0; y < bootInfo->height; y++) {
        uint32_t* row = fb + y * bootInfo->pixels_per_scanline;
        for (uint32_t x = 0; x < bootInfo->pixels_per_scanline; x++)
            row[x] = SCREEN_BG;
    }
}

static void print_total_ram(BootInfo* bootInfo)
{
    uint8_t* mmap = (uint8_t*)bootInfo->memory_map;
    uint64_t conventional_ram_bytes = 0;

    for (uint64_t offset = 0;
         offset < bootInfo->memory_map_size;
         offset += bootInfo->memory_map_descriptor_size) {
        EFI_MEMORY_DESCRIPTOR* desc =
            (EFI_MEMORY_DESCRIPTOR*)(mmap + offset);

        if (desc->Type == 7)
            conventional_ram_bytes += desc->NumberOfPages * 4096ULL;
    }

    print("Usable RAM: ");
    print_dec(conventional_ram_bytes / (1024 * 1024));
    print(" MiB\n");
}

static void print_memory_map(BootInfo* bootInfo, uint64_t limit)
{
    uint8_t* mmap = (uint8_t*)bootInfo->memory_map;
    uint64_t entries = bootInfo->memory_map_size /
                       bootInfo->memory_map_descriptor_size;

    if (limit > entries)
        limit = entries;

    print("Memory Map Entries:\n");
    for (uint64_t i = 0; i < limit; i++) {
        EFI_MEMORY_DESCRIPTOR* desc =
            (EFI_MEMORY_DESCRIPTOR*)(mmap + i * bootInfo->memory_map_descriptor_size);

        print("Type ");
        print_dec(desc->Type);
        print(" | Addr ");
        print_hex(desc->PhysicalStart);
        print(" | Pages ");
        print_dec(desc->NumberOfPages);
        print("\n");
    }
}

static void print_boot_summary(BootInfo* bootInfo)
{
    print("SamOS Kernel\n\n");
    print_total_ram(bootInfo);

    print("Memory map size: ");
    print_dec(bootInfo->memory_map_size);
    print("\nDescriptor size: ");
    print_dec(bootInfo->memory_map_descriptor_size);
    print("\nFramebuffer: ");
    print_hex((uint64_t)bootInfo->framebuffer);
    print("\nKernel start: ");
    print_hex((uint64_t)&_kernel_start);
    print("\nKernel end: ");
    print_hex((uint64_t)&_kernel_end);
    print("\n\n");

    print_memory_map(bootInfo, 16);
    print("\n");
}

static void init_display(BootInfo* bootInfo)
{
    display_init(
        (uint32_t*)bootInfo->framebuffer,
        bootInfo->width,
        bootInfo->height,
        bootInfo->pixels_per_scanline
    );
    clear_framebuffer(bootInfo);
}

static void init_descriptor_tables(void)
{
    gdt_init();
    tss_init();
    idt_init();
}

static void init_irq_devices(void)
{
    irq_controller_init();
    timer_init(TIMER_HZ);
    keyboard_init();
    rtc_init();
}

static void init_memory(BootInfo* bootInfo)
{
    paging_init(bootInfo);

    // Reload descriptor tables after switching to the kernel-owned CR3.
    gdt_init();
    tss_init();

    pmm_init(bootInfo);
    kmalloc_init();

    uint64_t fb_pixels = (uint64_t)display_get_height() *
                         (uint64_t)display_get_pitch();
    uint64_t fb_bytes = fb_pixels * 4;
    uint64_t fb_pages = (fb_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    uint32_t* shadow = (uint32_t*)pmm_alloc_pages(fb_pages);
    kassert(shadow != 0, "display shadow buffer allocation failed");

    display_set_shadow(shadow);
    for (uint64_t i = 0; i < fb_pixels; i++)
        shadow[i] = SCREEN_BG;

    vmm_init();
    print("VMM initialized\n");
}

static void run_memory_smoke_tests(void)
{
    void* page = pmm_alloc_page();
    kassert(page != 0, "PMM smoke test: allocation failed");
    kassert(((uint64_t)page & (PAGE_SIZE - 1)) == 0,
            "PMM smoke test: page is not aligned");
    pmm_free_page(page);

    AddressSpace* test_as = vmm_create_address_space();
    kassert(test_as != 0, "VMM smoke test: address space allocation failed");

    void* phys = pmm_alloc_page();
    kassert(phys != 0, "VMM smoke test: backing page allocation failed");

    uint64_t test_va = 0x8000000000ULL;
    vmm_map(test_as, test_va, (uint64_t)phys,
            VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
    kassert(vmm_virt_to_phys(test_as, test_va) == (uint64_t)phys,
            "VMM smoke test: virtual address did not resolve");

    vmm_unmap(test_as, test_va);
    pmm_free_page(phys);
    vmm_destroy_address_space(test_as);

    print("PMM/VMM smoke tests passed\n");
}

static void run_heap_smoke_tests(void)
{
    void* small = kmalloc(24);
    void* medium = kmalloc(800);
    void* zeroed = kzalloc(64);

    kassert(small != 0, "heap smoke test: small allocation failed");
    kassert(medium != 0, "heap smoke test: medium allocation failed");
    kassert(zeroed != 0, "heap smoke test: zeroed allocation failed");
    kassert(((uint64_t)small & 0xFULL) == 0, "heap smoke test: small allocation alignment failed");
    kassert(((uint64_t)medium & 0xFULL) == 0, "heap smoke test: medium allocation alignment failed");

    uint8_t* z = (uint8_t*)zeroed;
    for (uint64_t i = 0; i < 64; i++)
        kassert(z[i] == 0, "heap smoke test: kzalloc returned dirty memory");

    kfree(small);
    kfree(medium);
    kfree(zeroed);
    klog_info("Heap smoke tests passed");
}

static void enable_interrupts(void)
{
    irq_unmask(0);
    irq_unmask(1);
    __asm__ volatile ("sti");
}

static void print_irq_boot_banner(void)
{
    print("IRQ diagnostics:\n");
    print("  backend: ");
    print(irq_controller_name());
    print("\n");
    print("  vector map: IRQ0->0x20 IRQ1->0x21\n");
    print("  timer source: PIT ");
    print_dec((uint64_t)timer_get_frequency());
    print(" Hz\n");
    irq_dump_routes();
}

void kernel_main(BootInfo* bootInfo)
{
    if (!bootInfo)
        halt_forever();

    klog_init();
    init_display(bootInfo);
    kassert(bootInfo->framebuffer != 0, "bootinfo: missing framebuffer");
    kassert(bootInfo->memory_map != 0, "bootinfo: missing memory map");
    kassert(bootInfo->memory_map_descriptor_size != 0,
            "bootinfo: invalid memory map descriptor size");

    print_boot_summary(bootInfo);
    init_descriptor_tables();
    init_irq_devices();
    init_memory(bootInfo);
    run_memory_smoke_tests();
    run_heap_smoke_tests();
    acpi_init(bootInfo);
    irq_try_enable_apic();
    print("IRQ mode: ");
    print(irq_controller_name());
    print("\n");
    print_irq_boot_banner();
    if (!irq_backend_health())
        klog_warn("IRQ backend health has faults; safe-mode fallback recommended next boot");

    task_init();
    process_init();

    int shell_id = task_create_kernel(shell_task);
    int idle_id  = task_create_kernel(idle_task);
    (void)idle_id;

    // tell keyboard which task to wake on keypress
    keyboard_set_shell_task(shell_id);

    // Boot two user processes for v1 multiprogram userspace.
    process_spawn_builtin(SPAWN_IMAGE_HELLO);
    process_spawn_builtin(SPAWN_IMAGE_BURN);

    enable_interrupts();

    display_clear();
    print("SamOS\n");
    display_flush();

    schedule();      // enters shell_task — never returns
    halt_forever();
}
