#ifndef ACPI_H
#define ACPI_H

#include "bootinfo.h"
#include "types.h"

typedef struct {
    uint8_t source_irq;
    uint32_t gsi;
    uint16_t flags;
    uint8_t valid;
} AcpiIsoOverride;

typedef struct {
    uint8_t present;
    uint32_t lapic_addr;
    uint32_t ioapic_addr;
    uint32_t ioapic_gsi_base;
    uint32_t cpu_lapic_count;
    uint32_t iso_count;
    AcpiIsoOverride iso[16];
} AcpiMadtInfo;

void acpi_init(BootInfo* bootInfo);
void* acpi_find_table(const char signature[4]);
const AcpiMadtInfo* acpi_get_madt_info(void);
int acpi_get_irq_override(uint8_t irq, uint32_t* out_gsi, uint16_t* out_flags);
void shutdown(void);

#endif
