#include "acpi.h"
#include "klog.h"
#include "io.h"

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed)) RsdpV1;

typedef struct {
    RsdpV1 first;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) RsdpV2;

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) AcpiSdtHeader;

typedef struct {
    AcpiSdtHeader header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed)) Madt;

typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) MadtEntryHeader;

static AcpiSdtHeader* rsdt = 0;
static AcpiSdtHeader* xsdt = 0;
static AcpiMadtInfo g_madt;

static uint16_t g_pm1a_cnt_port = 0;
static uint16_t g_pm1b_cnt_port = 0;
static uint16_t g_slp_typa = 0;
static uint16_t g_slp_typb = 0;
static uint32_t g_smi_cmd = 0;
static uint8_t  g_acpi_enable = 0;
static int      g_have_acpi_poweroff = 0;

static int memeq(const char* a, const char* b, uint64_t len)
{
    for (uint64_t i = 0; i < len; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static uint8_t checksum(void* ptr, uint32_t len)
{
    uint8_t sum = 0;
    uint8_t* bytes = (uint8_t*)ptr;
    for (uint32_t i = 0; i < len; i++)
        sum += bytes[i];
    return sum;
}

static int table_valid(AcpiSdtHeader* table)
{
    return table && checksum(table, table->length) == 0;
}

void* acpi_find_table(const char signature[4])
{
    if (xsdt && table_valid(xsdt)) {
        uint32_t entries = (xsdt->length - sizeof(AcpiSdtHeader)) / 8;
        uint64_t* ptrs = (uint64_t*)((uint8_t*)xsdt + sizeof(AcpiSdtHeader));

        for (uint32_t i = 0; i < entries; i++) {
            AcpiSdtHeader* table = (AcpiSdtHeader*)ptrs[i];
            if (table_valid(table) && memeq(table->signature, signature, 4))
                return table;
        }
    }

    if (rsdt && table_valid(rsdt)) {
        uint32_t entries = (rsdt->length - sizeof(AcpiSdtHeader)) / 4;
        uint32_t* ptrs = (uint32_t*)((uint8_t*)rsdt + sizeof(AcpiSdtHeader));

        for (uint32_t i = 0; i < entries; i++) {
            AcpiSdtHeader* table = (AcpiSdtHeader*)(uint64_t)ptrs[i];
            if (table_valid(table) && memeq(table->signature, signature, 4))
                return table;
        }
    }

    return 0;
}

static void print_signature(const char sig[4])
{
    char s[5] = { sig[0], sig[1], sig[2], sig[3], 0 };
    klog_info(s);
}

static void list_tables(void)
{
    if (xsdt && table_valid(xsdt)) {
        uint32_t entries = (xsdt->length - sizeof(AcpiSdtHeader)) / 8;
        uint64_t* ptrs = (uint64_t*)((uint8_t*)xsdt + sizeof(AcpiSdtHeader));
        klog_info("ACPI XSDT tables:");
        for (uint32_t i = 0; i < entries; i++) {
            AcpiSdtHeader* table = (AcpiSdtHeader*)ptrs[i];
            if (table_valid(table))
                print_signature(table->signature);
        }
        return;
    }

    if (rsdt && table_valid(rsdt)) {
        uint32_t entries = (rsdt->length - sizeof(AcpiSdtHeader)) / 4;
        uint32_t* ptrs = (uint32_t*)((uint8_t*)rsdt + sizeof(AcpiSdtHeader));
        klog_info("ACPI RSDT tables:");
        for (uint32_t i = 0; i < entries; i++) {
            AcpiSdtHeader* table = (AcpiSdtHeader*)(uint64_t)ptrs[i];
            if (table_valid(table))
                print_signature(table->signature);
        }
    }
}

static void parse_madt(void)
{
    Madt* madt = (Madt*)acpi_find_table("APIC");
    if (!madt) {
        klog_warn("ACPI MADT/APIC table not found");
        return;
    }

    g_madt.present = 1;
    g_madt.lapic_addr = madt->local_apic_address;

    klog_info("ACPI MADT found");
    klog_info("Local APIC address:");
    klog_hex(madt->local_apic_address);
    klog_newline();

    uint8_t* entry = (uint8_t*)madt + sizeof(Madt);
    uint8_t* end = (uint8_t*)madt + madt->header.length;

    while (entry + sizeof(MadtEntryHeader) <= end) {
        MadtEntryHeader* hdr = (MadtEntryHeader*)entry;
        if (hdr->length < sizeof(MadtEntryHeader) || entry + hdr->length > end)
            break;

        if (hdr->type == 0 && hdr->length >= 8) {
            klog_info("MADT CPU local APIC entry");
            g_madt.cpu_lapic_count++;
        } else if (hdr->type == 1 && hdr->length >= 12) {
            uint32_t ioapic_addr = *(uint32_t*)(entry + 4);
            uint32_t gsi_base = *(uint32_t*)(entry + 8);
            g_madt.ioapic_addr = ioapic_addr;
            g_madt.ioapic_gsi_base = gsi_base;
            klog_info("MADT IOAPIC address:");
            klog_hex(ioapic_addr);
            klog_newline();
            klog_info("MADT IOAPIC GSI base:");
            klog_dec(gsi_base);
            klog_newline();
        } else if (hdr->type == 2 && hdr->length >= 10) {
            uint8_t source = *(uint8_t*)(entry + 3);
            uint32_t gsi = *(uint32_t*)(entry + 4);
            uint16_t flags = *(uint16_t*)(entry + 8);
            if (g_madt.iso_count < 16) {
                AcpiIsoOverride* iso = &g_madt.iso[g_madt.iso_count++];
                iso->source_irq = source;
                iso->gsi = gsi;
                iso->flags = flags;
                iso->valid = 1;
            }
            klog_info("MADT interrupt source override IRQ:");
            klog_dec(source);
            klog_info("MADT interrupt source override GSI:");
            klog_dec(gsi);
            klog_info("MADT interrupt source override flags:");
            klog_hex(flags);
            klog_newline();
        }

        entry += hdr->length;
    }
}

static uint16_t read_u16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t* p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int aml_extract_s5_types(const uint8_t* dsdt, uint32_t dsdt_len,
                                uint16_t* out_typa, uint16_t* out_typb)
{
    if (!dsdt || dsdt_len < sizeof(AcpiSdtHeader) + 8)
        return 0;

    const uint8_t* aml = dsdt + sizeof(AcpiSdtHeader);
    uint32_t aml_len = dsdt_len - sizeof(AcpiSdtHeader);

    for (uint32_t i = 0; i + 7 < aml_len; i++) {
        if (aml[i + 0] != '_' || aml[i + 1] != 'S' ||
            aml[i + 2] != '5' || aml[i + 3] != '_')
            continue;

        // AML NameOp may be prefixed with '\' or '^'.
        if (i == 0) continue;
        uint8_t pre = aml[i - 1];
        if (pre != 0x08) {
            if (!(i >= 2 && (aml[i - 2] == 0x08) &&
                  (pre == '\\' || pre == '^')))
                continue;
        }

        uint32_t pos = i + 4;
        if (pos >= aml_len) continue;
        if (aml[pos] != 0x12) continue; // PackageOp
        pos++;
        if (pos >= aml_len) continue;

        // Skip PkgLength encoding (1-4 bytes).
        uint8_t lead = aml[pos++];
        uint8_t follow = (lead >> 6) & 0x3;
        if (pos + follow >= aml_len) continue;
        pos += follow;
        if (pos >= aml_len) continue;

        // NumElements
        pos++;
        if (pos >= aml_len) continue;

        uint8_t a = aml[pos];
        if (a == 0x0A) { // BytePrefix
            if (pos + 1 >= aml_len) continue;
            a = aml[++pos];
        } else if (a == 0x0B) { // WordPrefix
            if (pos + 2 >= aml_len) continue;
            a = (uint8_t)read_u16(&aml[pos + 1]);
            pos += 2;
        }

        pos++;
        if (pos >= aml_len) continue;

        uint8_t b = aml[pos];
        if (b == 0x0A) {
            if (pos + 1 >= aml_len) continue;
            b = aml[++pos];
        } else if (b == 0x0B) {
            if (pos + 2 >= aml_len) continue;
            b = (uint8_t)read_u16(&aml[pos + 1]);
            pos += 2;
        }

        *out_typa = (uint16_t)a;
        *out_typb = (uint16_t)b;
        return 1;
    }

    return 0;
}

static void parse_fadt_poweroff(void)
{
    AcpiSdtHeader* fadt = (AcpiSdtHeader*)acpi_find_table("FACP");
    if (!fadt || !table_valid(fadt)) {
        klog_warn("ACPI FADT not found");
        return;
    }

    const uint8_t* f = (const uint8_t*)fadt;
    uint32_t len = fadt->length;

    if (len < 90) {
        klog_warn("ACPI FADT too small for poweroff fields");
        return;
    }

    uint32_t dsdt_addr = read_u32(f + 40);
    g_smi_cmd = read_u32(f + 48);
    g_acpi_enable = f[52];
    g_pm1a_cnt_port = (uint16_t)read_u32(f + 64);
    g_pm1b_cnt_port = (uint16_t)read_u32(f + 68);

    AcpiSdtHeader* dsdt = (AcpiSdtHeader*)(uint64_t)dsdt_addr;
    if (!dsdt || !table_valid(dsdt)) {
        klog_warn("ACPI DSDT invalid");
        return;
    }

    if (!aml_extract_s5_types((const uint8_t*)dsdt, dsdt->length,
                              &g_slp_typa, &g_slp_typb)) {
        klog_warn("ACPI _S5 object not found in DSDT");
        return;
    }

    if (g_pm1a_cnt_port == 0) {
        klog_warn("ACPI PM1a control block missing");
        return;
    }

    g_have_acpi_poweroff = 1;
    klog_info("ACPI S5 poweroff path ready");
}

void acpi_init(BootInfo* bootInfo)
{
    for (uint32_t i = 0; i < sizeof(g_madt); i++)
        ((uint8_t*)&g_madt)[i] = 0;

    if (!bootInfo->rsdp) {
        klog_warn("ACPI RSDP not provided by bootloader");
        return;
    }

    RsdpV2* rsdp2 = (RsdpV2*)bootInfo->rsdp;
    if (!memeq(rsdp2->first.signature, "RSD PTR ", 8)) {
        klog_error("ACPI RSDP signature invalid");
        return;
    }

    if (checksum(&rsdp2->first, sizeof(RsdpV1)) != 0) {
        klog_error("ACPI RSDP checksum invalid");
        return;
    }

    rsdt = (AcpiSdtHeader*)(uint64_t)rsdp2->first.rsdt_address;

    if (rsdp2->first.revision >= 2 &&
        rsdp2->length >= sizeof(RsdpV2) &&
        checksum(rsdp2, rsdp2->length) == 0) {
        xsdt = (AcpiSdtHeader*)rsdp2->xsdt_address;
        klog_info("ACPI 2.0+ XSDT available");
    } else {
        klog_info("ACPI 1.0 RSDT available");
    }

    list_tables();
    parse_madt();
    parse_fadt_poweroff();
}

const AcpiMadtInfo* acpi_get_madt_info(void)
{
    return &g_madt;
}

int acpi_get_irq_override(uint8_t irq, uint32_t* out_gsi, uint16_t* out_flags)
{
    for (uint32_t i = 0; i < g_madt.iso_count; i++) {
        AcpiIsoOverride* iso = &g_madt.iso[i];
        if (!iso->valid) continue;
        if (iso->source_irq != irq) continue;

        if (out_gsi) *out_gsi = iso->gsi;
        if (out_flags) *out_flags = iso->flags;
        return 1;
    }

    if (out_gsi) *out_gsi = irq;
    if (out_flags) *out_flags = 0;
    return 0;
}


// void shutdown() {
//     outw(PM1a_CNT, SLP_TYP | (1 << 13)); // SLP_EN
// }
void shutdown(void)
{
    __asm__ volatile ("cli");

    // Preferred path: ACPI S5 sleep transition from FADT/DSDT.
    if (g_have_acpi_poweroff) {
        // Enable ACPI mode when firmware still uses legacy mode.
        if ((inw(g_pm1a_cnt_port) & 1) == 0 && g_smi_cmd != 0 && g_acpi_enable != 0) {
            outb((uint16_t)g_smi_cmd, g_acpi_enable);
            for (int i = 0; i < 30000; i++) {
                if (inw(g_pm1a_cnt_port) & 1)
                    break;
                io_wait();
            }
        }

        uint16_t val_a = (inw(g_pm1a_cnt_port) & 0xE3FFU) |
                         (uint16_t)(g_slp_typa << 10) | (1U << 13);
        outw(g_pm1a_cnt_port, val_a);

        if (g_pm1b_cnt_port != 0) {
            uint16_t val_b = (inw(g_pm1b_cnt_port) & 0xE3FFU) |
                             (uint16_t)(g_slp_typb << 10) | (1U << 13);
            outw(g_pm1b_cnt_port, val_b);
        }
    }

    // Fallbacks for common virtual platforms.
    outw(0x604, 0x2000); // QEMU
    outw(0xB004, 0x2000); // Bochs/older QEMU
    outw(0x4004, 0x3400); // VirtualBox

    while (1)
        __asm__ volatile ("hlt");
}
