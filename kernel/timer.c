#include "timer.h"
#include "display.h"

static volatile uint64_t ticks = 0;
static volatile int flush_needed = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

// PIT (Programmable Interval Timer) runs at 1193182 Hz
// divisor = 1193182 / frequency
void timer_init(uint32_t frequency)
{
    uint32_t divisor = 1193182 / frequency;

    outb(0x43, 0x36);                      // channel 0, lobyte/hibyte, rate generator
    outb(0x40, divisor & 0xFF);            // low byte
    outb(0x40, (divisor >> 8) & 0xFF);    // high byte

    print("Timer initialized at ");
    print_dec(frequency);
    print(" Hz\n");
}

void timer_handler(void)
{
    ticks++;
    if (ticks % 2 == 0)
        flush_needed = 1;  // just set flag, don't flush here
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
    uint64_t t;
    __asm__ volatile ("cli");
    t = ticks;
    __asm__ volatile ("sti");
    return t;
}