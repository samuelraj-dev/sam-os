#include "keyboard.h"
#include "display.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

void keyboard_init(void)
{
    // flush whatever is in the buffer
    while (inb(0x64) & 0x01) inb(0x60);

    // read current config
    outb(0x64, 0x20);
    while (!(inb(0x64) & 0x01));
    uint8_t config = inb(0x60);

    print("PS2 config: "); print_hex(config); print("\n");

    // enable translation + IRQ1, don't touch anything else
    config |= (1 << 6);  // scancode translation
    config |= (1 << 0);  // IRQ1 enabled

    // write back
    outb(0x64, 0x60);
    while (inb(0x64) & 0x02);
    outb(0x60, config);

    // enable port 1
    outb(0x64, 0xAE);

    print("Keyboard initialized\n");
}

void keyboard_handler(void)
{
    uint8_t scancode = inb(0x60);
    
    // ignore key releases (bit 7 set)
    if (scancode & 0x80) return;

    if (scancode < 128 && scancode_map[scancode]) {
        char str[2] = { scancode_map[scancode], 0 };
        print(str);
    }
}