#include "irq.h"
#include "pic.h"
#include "apic.h"
#include "acpi.h"
#include "klog.h"
#include "display.h"

#define IRQ_VECTOR_BASE 0x20
#define APIC_SWITCHOVER_DEFAULT 1

static IRQBackend g_backend = IRQ_BACKEND_PIC;
static uint32_t g_irq_gsi[16];
static IRQStats g_stats;

static int pic_is_spurious_irq7(void)
{
    uint8_t isr_val;
    __asm__ volatile (
        "mov $0x0B, %%al\n"
        "out %%al, $0x20\n"
        "in $0x20, %%al\n"
        : "=a"(isr_val)
        :
        : "memory"
    );
    return (isr_val & 0x80) == 0;
}

void irq_controller_init(void)
{
    for (uint8_t i = 0; i < 16; i++)
        g_irq_gsi[i] = i;

    g_stats.timer_irq_count = 0;
    g_stats.keyboard_irq_count = 0;
    g_stats.spurious_irq_count = 0;
    g_stats.eoi_count = 0;
    g_stats.apic_route_failures = 0;
    g_stats.health_faults = 0;

    pic_init();
    g_backend = IRQ_BACKEND_PIC;
    klog_info("IRQ controller: legacy PIC backend active");
}

int irq_try_enable_apic(void)
{
    const AcpiMadtInfo* madt = acpi_get_madt_info();
    if (!madt || !madt->present) {
        klog_warn("IRQ controller: MADT not available, keeping PIC");
        g_stats.health_faults++;
        return 0;
    }

    ApicAddresses addrs;
    addrs.lapic_addr = madt->lapic_addr;
    addrs.ioapic_addr = madt->ioapic_addr;
    addrs.ioapic_gsi_base = madt->ioapic_gsi_base;
    apic_set_addresses(addrs);

    if (!apic_enable()) {
        klog_warn("IRQ controller: APIC enable failed, keeping PIC");
        g_stats.health_faults++;
        return 0;
    }

    uint8_t gsi_claimed[256];
    for (uint32_t i = 0; i < 256; i++)
        gsi_claimed[i] = 0;

    uint8_t critical_irqs[2] = {0, 1};
    for (uint8_t i = 0; i < 2; i++) {
        uint8_t irq = critical_irqs[i];
        uint32_t gsi = irq;
        uint16_t flags = 0;
        acpi_get_irq_override(irq, &gsi, &flags);
        g_irq_gsi[irq] = gsi;

        if (gsi < madt->ioapic_gsi_base)
            continue;

        if (!apic_route_irq(gsi, (uint8_t)(IRQ_VECTOR_BASE + irq), flags)) {
            klog_warn("IRQ controller: IOAPIC routing failed for an IRQ");
            g_stats.apic_route_failures++;
            g_stats.health_faults++;
            return 0;
        }

        if (gsi < 256)
            gsi_claimed[gsi] = 1;
    }

    for (uint8_t irq = 0; irq < 16; irq++) {
        if (irq == 0 || irq == 1)
            continue;

        uint32_t gsi = irq;
        uint16_t flags = 0;
        acpi_get_irq_override(irq, &gsi, &flags);
        g_irq_gsi[irq] = gsi;

        if (gsi < madt->ioapic_gsi_base)
            continue;

        if (gsi < 256 && gsi_claimed[gsi]) {
            klog_warn("IRQ controller: skipping duplicate GSI route");
            continue;
        }

        if (!apic_route_irq(gsi, (uint8_t)(IRQ_VECTOR_BASE + irq), flags)) {
            klog_warn("IRQ controller: IOAPIC routing failed for an IRQ");
            g_stats.apic_route_failures++;
            g_stats.health_faults++;
            return 0;
        }

        if (gsi < 256)
            gsi_claimed[gsi] = 1;
    }

    if (!APIC_SWITCHOVER_DEFAULT) {
        klog_warn("IRQ controller: APIC routed but switchover disabled, keeping PIC");
        return 0;
    }

    apic_mask_legacy_pic();
    g_backend = IRQ_BACKEND_APIC;
    klog_info("IRQ controller: APIC/IOAPIC backend active");
    return 1;
}

void irq_unmask(uint8_t irq)
{
    if (irq >= 16) return;
    if (g_backend == IRQ_BACKEND_APIC) {
        apic_set_irq_mask(g_irq_gsi[irq], 0);
        return;
    }
    pic_unmask(irq);
}

void irq_mask(uint8_t irq)
{
    if (irq >= 16) return;
    if (g_backend == IRQ_BACKEND_APIC) {
        apic_set_irq_mask(g_irq_gsi[irq], 1);
        return;
    }
    pic_mask(irq);
}

void irq_send_eoi(uint8_t irq)
{
    g_stats.eoi_count++;
    (void)irq;
    if (g_backend == IRQ_BACKEND_APIC) {
        apic_send_eoi();
        return;
    }
    pic_send_eoi(irq);
}

int irq_is_spurious(uint8_t irq)
{
    if (g_backend != IRQ_BACKEND_PIC)
        return 0;

    if (irq == 7) {
        int spurious = pic_is_spurious_irq7();
        if (spurious)
            g_stats.spurious_irq_count++;
        return spurious;
    }

    return 0;
}

IRQBackend irq_controller_backend(void)
{
    return g_backend;
}

const char* irq_controller_name(void)
{
    if (g_backend == IRQ_BACKEND_APIC)
        return "APIC/IOAPIC";
    return "PIC";
}

uint32_t irq_resolve_gsi(uint8_t irq)
{
    if (irq < 16)
        return g_irq_gsi[irq];
    return irq;
}

void irq_get_stats(IRQStats* out)
{
    if (!out) return;
    *out = g_stats;
}

void irq_dump_routes(void)
{
    print("irq: backend=");
    print(irq_controller_name());
    print(" base_vector=");
    print_hex(IRQ_VECTOR_BASE);
    print("\n");

    for (uint8_t irq = 0; irq < 16; irq++) {
        print("irq: irq");
        print_dec(irq);
        print(" -> gsi ");
        print_dec(g_irq_gsi[irq]);
        print(" vec ");
        print_hex((uint64_t)(IRQ_VECTOR_BASE + irq));
        print("\n");
    }
}

int irq_backend_health(void)
{
    return g_stats.health_faults == 0;
}

void irq_note_timer_irq(void)
{
    g_stats.timer_irq_count++;
}

void irq_note_keyboard_irq(void)
{
    g_stats.keyboard_irq_count++;
}
