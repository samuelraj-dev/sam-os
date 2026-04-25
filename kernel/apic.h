#ifndef APIC_H
#define APIC_H

#include "types.h"

typedef struct {
    uint32_t lapic_addr;
    uint32_t ioapic_addr;
    uint32_t ioapic_gsi_base;
} ApicAddresses;

void apic_set_addresses(ApicAddresses addrs);
int  apic_can_enable(void);
int  apic_enable(void);
void apic_send_eoi(void);
void apic_mask_legacy_pic(void);
int  apic_route_irq(uint32_t gsi, uint8_t vector, uint16_t flags);
void apic_set_irq_mask(uint32_t gsi, int masked);
uint8_t apic_lapic_id(void);

#endif
