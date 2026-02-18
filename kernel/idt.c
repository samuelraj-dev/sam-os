#include "idt.h"
#include "gdt.h"
#include "pic.h"
#include "timer.h"
#include "keyboard.h"
#include "display.h"

static IDTEntry idt[256];
static IDTDescriptor idtr;

// declare all ISR stubs from isr.asm
extern void isr0(void);  extern void isr1(void);
extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);
extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void);
extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void);
extern void isr20(void);
extern void isr32(void); extern void isr33(void);
extern void isr34(void); extern void isr35(void);
extern void isr36(void); extern void isr37(void);
extern void isr38(void); extern void isr39(void);
extern void isr40(void); extern void isr41(void);
extern void isr42(void); extern void isr43(void);
extern void isr44(void); extern void isr45(void);
extern void isr46(void); extern void isr47(void);

static void set_entry(uint8_t vector, void* handler,
                      uint8_t ist, uint8_t type_attr)
{
    uint64_t addr = (uint64_t)handler;
    idt[vector].offset_low  = addr & 0xFFFF;
    idt[vector].selector    = GDT_KERNEL_CODE;
    idt[vector].ist         = ist;
    idt[vector].type_attr   = type_attr;
    idt[vector].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vector].reserved    = 0;
}

void idt_init(void)
{
    // zero all entries first
    for (int i = 0; i < 256; i++) {
        uint8_t* p = (uint8_t*)&idt[i];
        for (int j = 0; j < sizeof(IDTEntry); j++) p[j] = 0;
    }

    // 0x8E = present, ring 0, interrupt gate (clears IF on entry)
    // 0xEF = present, ring 0, trap gate    (keeps IF on entry)
    // use interrupt gates for hardware IRQs, trap gates for exceptions

    set_entry(0,  isr0,  0, 0x8E);
    set_entry(1,  isr1,  0, 0x8E);
    set_entry(2,  isr2,  0, 0x8E);
    set_entry(3,  isr3,  0, 0x8E);
    set_entry(4,  isr4,  0, 0x8E);
    set_entry(5,  isr5,  0, 0x8E);
    set_entry(6,  isr6,  0, 0x8E);
    set_entry(7,  isr7,  0, 0x8E);
    set_entry(8,  isr8,  1, 0x8E); // double fault — use IST1
    set_entry(9,  isr9,  0, 0x8E);
    set_entry(10, isr10, 0, 0x8E);
    set_entry(11, isr11, 0, 0x8E);
    set_entry(12, isr12, 0, 0x8E);
    set_entry(13, isr13, 0, 0x8E);
    set_entry(14, isr14, 0, 0x8E);
    set_entry(15, isr15, 0, 0x8E);
    set_entry(16, isr16, 0, 0x8E);
    set_entry(17, isr17, 0, 0x8E);
    set_entry(18, isr18, 0, 0x8E);
    set_entry(19, isr19, 0, 0x8E);
    set_entry(20, isr20, 0, 0x8E);

    // IRQs
    // for (int i = 32; i < 48; i++)
    //     set_entry(i, (&isr32)[i - 32], 0, 0x8E);
    set_entry(32, isr32, 0, 0x8E);
    set_entry(33, isr33, 0, 0x8E);
    set_entry(34, isr34, 0, 0x8E);
    set_entry(35, isr35, 0, 0x8E);
    set_entry(36, isr36, 0, 0x8E);
    set_entry(37, isr37, 0, 0x8E);
    set_entry(38, isr38, 0, 0x8E);
    set_entry(39, isr39, 0, 0x8E);
    set_entry(40, isr40, 0, 0x8E);
    set_entry(41, isr41, 0, 0x8E);
    set_entry(42, isr42, 0, 0x8E);
    set_entry(43, isr43, 0, 0x8E);
    set_entry(44, isr44, 0, 0x8E);
    set_entry(45, isr45, 0, 0x8E);
    set_entry(46, isr46, 0, 0x8E);
    set_entry(47, isr47, 0, 0x8E);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)&idt;

    __asm__ volatile ("lidt %0" :: "m"(idtr));

    print("IDT loaded\n");
}

static const char* exception_names[] = {
    "Divide By Zero",       "Debug",
    "NMI",                  "Breakpoint",
    "Overflow",             "Bound Range",
    "Invalid Opcode",       "Device Not Available",
    "Double Fault",         "Coprocessor Overrun",
    "Invalid TSS",          "Segment Not Present",
    "Stack Fault",          "General Protection Fault",
    "Page Fault",           "Reserved",
    "x87 FPU",              "Alignment Check",
    "Machine Check",        "SIMD FPU",
    "Virtualization Fault"
};

void interrupt_handler(InterruptFrame* frame)
{
    if (frame->vector < 32) {
        // CPU exception — print and halt
        print("\n--- EXCEPTION ---\n");
        if (frame->vector <= 20)
            print(exception_names[frame->vector]);
        print("\nVector: ");  print_dec(frame->vector);
        print("\nError:  ");  print_hex(frame->error_code);
        print("\nRIP:    ");  print_hex(frame->rip);
        print("\nRSP:    ");  print_hex(frame->rsp);
        print("\nRFLAGS: "); print_hex(frame->rflags);
        if (frame->vector == 14) {
            uint64_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            print("\nCR2:    "); print_hex(cr2);
        }
        print("\n--- HALTED ---\n");
        while (1) __asm__ volatile ("hlt");
    }

    // hardware IRQ
    uint8_t irq = frame->vector - 32;

    // check for spurious IRQ
    if (irq == 7) {
        // check if real by reading ISR
        uint8_t isr;
        __asm__ volatile (
            "mov $0x0B, %%al\n"
            "out %%al, $0x20\n"
            "in $0x20, %%al\n"
            : "=a"(isr)
        );
        if (!(isr & 0x80)) return;  // spurious, don't send EOI
    }

    switch (irq) {
        case 0: timer_handler();    break;
        case 1: keyboard_handler(); break;
        default: break;
    }

    pic_send_eoi(irq);
}