#include "pic.h"
#include "display.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void io_wait(void) {
    outb(0x80, 0);  // write to unused port = ~1us delay
}

void pic_init(void)
{
    // save current masks
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    // ICW1 — start initialization sequence
    outb(PIC1_COMMAND, 0x11); io_wait();  // 0x11 = init + ICW4 needed
    outb(PIC2_COMMAND, 0x11); io_wait();

    // ICW2 — vector offsets
    outb(PIC1_DATA, 0x20); io_wait();  // master IRQs start at vector 0x20 (32)
    outb(PIC2_DATA, 0x28); io_wait();  // slave  IRQs start at vector 0x28 (40)

    // ICW3 — tell master/slave how they're connected
    outb(PIC1_DATA, 0x04); io_wait();  // master: slave on IRQ2 (bit 2)
    outb(PIC2_DATA, 0x02); io_wait();  // slave:  cascade identity = 2

    // ICW4 — set 8086 mode
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    // restore masks
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);

    // mask all IRQs to start — unmask only what you need
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    print("PIC remapped\n");
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8)
        outb(PIC2_COMMAND, PIC_EOI);  // slave needs EOI too
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_mask(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, inb(port) | (1 << irq));
}

void pic_unmask(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, inb(port) & ~(1 << irq));
}