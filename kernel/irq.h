#ifndef IRQ_H
#define IRQ_H

#include "types.h"

typedef enum {
    IRQ_BACKEND_PIC = 0,
    IRQ_BACKEND_APIC = 1
} IRQBackend;

typedef struct {
    uint64_t timer_irq_count;
    uint64_t keyboard_irq_count;
    uint64_t spurious_irq_count;
    uint64_t eoi_count;
    uint64_t apic_route_failures;
    uint64_t health_faults;
} IRQStats;

void irq_controller_init(void);
int  irq_try_enable_apic(void);
void irq_unmask(uint8_t irq);
void irq_mask(uint8_t irq);
void irq_send_eoi(uint8_t irq);
int  irq_is_spurious(uint8_t irq);
IRQBackend irq_controller_backend(void);
const char* irq_controller_name(void);
uint32_t irq_resolve_gsi(uint8_t irq);
void irq_get_stats(IRQStats* out);
void irq_dump_routes(void);
int  irq_backend_health(void);
void irq_note_timer_irq(void);
void irq_note_keyboard_irq(void);

#endif
