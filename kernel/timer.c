#include "timer.h"
#include "display.h"
#include "panic.h"
#include "irq.h"
#include "scheduler/task.h"
#include "io.h"

static volatile uint64_t ticks       = 0;
static volatile int      flush_needed = 0;
static uint32_t timer_hz = 0;

void timer_init(uint32_t frequency)
{
    if (frequency == 0)
        panic("timer_init: frequency must be non-zero");

    uint32_t divisor = 1193182 / frequency;
    if (divisor == 0)    divisor = 1;
    if (divisor > 0xFFFF) divisor = 0xFFFF;

    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    timer_hz = frequency;

    print("Timer initialized at ");
    print_dec(frequency);
    print(" Hz\n");
}

// called from isr32 assembly stub
// returns new task RSP (0 = no switch)
uint64_t timer_tick(uint64_t current_rsp)
{
    irq_send_eoi(0);
    irq_note_timer_irq();
    ticks++;
    flush_needed = 1;
    return schedule_on_tick(current_rsp);
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
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0" : "=r"(flags));
    __asm__ volatile ("cli");
    t = ticks;
    if (flags & (1ULL << 9))
        __asm__ volatile ("sti");
    return t;
}

uint32_t timer_get_frequency(void)
{
    return timer_hz;
}
