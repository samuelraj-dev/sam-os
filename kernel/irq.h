#ifndef IRQ_H
#define IRQ_H

#include "types.h"

void irq_controller_init(void);
void irq_unmask(uint8_t irq);
void irq_mask(uint8_t irq);
void irq_send_eoi(uint8_t irq);

#endif
