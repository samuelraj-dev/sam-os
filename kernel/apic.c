#include "apic.h"
#include "mmio.h"
#include "pic.h"
#include "klog.h"

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_ENABLE (1ULL << 11)

#define LAPIC_REG_ID       0x020
#define LAPIC_REG_EOI      0x0B0
#define LAPIC_REG_SVR      0x0F0

#define IOAPIC_REGSEL      0x00
#define IOAPIC_WINDOW      0x10
#define IOAPIC_REG_VER     0x01

static ApicAddresses g_addrs;

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value)
{
    __asm__ volatile (
        "wrmsr"
        :
        : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32))
    );
}

static uint32_t lapic_read(uint32_t reg)
{
    return mmio_read32((uint64_t)g_addrs.lapic_addr + reg);
}

static void lapic_write(uint32_t reg, uint32_t value)
{
    mmio_write32((uint64_t)g_addrs.lapic_addr + reg, value);
}

static uint32_t ioapic_read(uint8_t reg)
{
    uint64_t base = (uint64_t)g_addrs.ioapic_addr;
    mmio_write32(base + IOAPIC_REGSEL, reg);
    return mmio_read32(base + IOAPIC_WINDOW);
}

static void ioapic_write(uint8_t reg, uint32_t value)
{
    uint64_t base = (uint64_t)g_addrs.ioapic_addr;
    mmio_write32(base + IOAPIC_REGSEL, reg);
    mmio_write32(base + IOAPIC_WINDOW, value);
}

void apic_set_addresses(ApicAddresses addrs)
{
    g_addrs = addrs;
}

int apic_can_enable(void)
{
    return g_addrs.lapic_addr != 0 && g_addrs.ioapic_addr != 0;
}

int apic_enable(void)
{
    if (!apic_can_enable())
        return 0;

    uint64_t apic_base = rdmsr(IA32_APIC_BASE_MSR);
    apic_base |= IA32_APIC_BASE_ENABLE;
    wrmsr(IA32_APIC_BASE_MSR, apic_base);

    // Spurious Interrupt Vector Register: bit 8 enables LAPIC software.
    lapic_write(LAPIC_REG_SVR, 0x100 | 0xFF);

    klog_info("APIC: local APIC enabled");
    return 1;
}

void apic_send_eoi(void)
{
    if (!g_addrs.lapic_addr)
        return;
    lapic_write(LAPIC_REG_EOI, 0);
}

void apic_mask_legacy_pic(void)
{
    for (uint8_t i = 0; i < 16; i++)
        pic_mask(i);
}

uint8_t apic_lapic_id(void)
{
    if (!g_addrs.lapic_addr)
        return 0;
    return (uint8_t)(lapic_read(LAPIC_REG_ID) >> 24);
}

void apic_set_irq_mask(uint32_t gsi, int masked)
{
    if (!g_addrs.ioapic_addr) return;

    if (gsi < g_addrs.ioapic_gsi_base) return;

    uint32_t index = gsi - g_addrs.ioapic_gsi_base;
    uint8_t low_reg = (uint8_t)(0x10 + index * 2);

    uint32_t low = ioapic_read(low_reg);
    if (masked)
        low |= (1U << 16);
    else
        low &= ~(1U << 16);

    ioapic_write(low_reg, low);
}

int apic_route_irq(uint32_t gsi, uint8_t vector, uint16_t flags)
{
    if (!g_addrs.ioapic_addr)
        return 0;

    uint32_t ioapic_ver = ioapic_read(IOAPIC_REG_VER);
    uint32_t max_redir = (ioapic_ver >> 16) & 0xFF;

    if (gsi < g_addrs.ioapic_gsi_base)
        return 0;

    uint32_t index = gsi - g_addrs.ioapic_gsi_base;
    if (index > max_redir)
        return 0;

    uint8_t low_reg = (uint8_t)(0x10 + index * 2);
    uint8_t hi_reg = (uint8_t)(low_reg + 1);

    uint32_t low = vector;

    // MADT ISO flags: bit1 active-low, bit3 level-triggered.
    if (flags & 0x2)
        low |= (1U << 13);
    if (flags & 0x8)
        low |= (1U << 15);

    // Unmask.
    low &= ~(1U << 16);

    uint32_t high = ((uint32_t)apic_lapic_id()) << 24;

    ioapic_write(hi_reg, high);
    ioapic_write(low_reg, low);

    return 1;
}
