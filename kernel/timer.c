#include "timer.h"
#include "display.h"
#include "panic.h"

static volatile uint64_t ticks = 0;
static volatile int flush_needed = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

// PIT (Programmable Interval Timer) runs at 1193182 Hz
// divisor = 1193182 / frequency
void timer_init(uint32_t frequency)
{
    if (frequency == 0)
        panic("timer_init: frequency must be non-zero");

    uint32_t divisor = 1193182 / frequency;
    if (divisor == 0)
        divisor = 1;
    if (divisor > 0xFFFF)
        divisor = 0xFFFF;

    outb(0x43, 0x36);                      // channel 0, lobyte/hibyte, rate generator
    outb(0x40, divisor & 0xFF);            // low byte
    outb(0x40, (divisor >> 8) & 0xFF);    // high byte

    print("Timer initialized at ");
    print_dec(frequency);
    print(" Hz\n");
}

// void timer_handler(void)
// {
//     ticks++;
//     if (ticks % 2 == 0)
//         flush_needed = 1;  // just set flag, don't flush here
// }
void timer_handler(void)
{
    ticks++;
    flush_needed = 1;
}

int timer_flush_needed(void)
{
    if (flush_needed) {
        flush_needed = 0;
        return 1;
    }
    return 0;
}

uint64_t timer_get_ticks(void)
{
    // uint64_t t;
    // __asm__ volatile ("cli");
    // t = ticks;
    // __asm__ volatile ("sti");
    // return t;
    
    uint64_t t;
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0" : "=r"(flags));
    __asm__ volatile("cli");
    t = ticks;
    if (flags & (1 << 9))
        __asm__ volatile("sti");
    return t;
}
