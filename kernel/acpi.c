#include "acpi.h"
#include "klog.h"

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
        } else if (hdr->type == 1 && hdr->length >= 12) {
            uint32_t ioapic_addr = *(uint32_t*)(entry + 4);
            uint32_t gsi_base = *(uint32_t*)(entry + 8);
            klog_info("MADT IOAPIC address:");
            klog_hex(ioapic_addr);
            klog_newline();
            klog_info("MADT IOAPIC GSI base:");
            klog_dec(gsi_base);
            klog_newline();
        } else if (hdr->type == 2 && hdr->length >= 10) {
            uint8_t source = *(uint8_t*)(entry + 3);
            uint32_t gsi = *(uint32_t*)(entry + 4);
            klog_info("MADT interrupt source override IRQ:");
            klog_dec(source);
            klog_info("MADT interrupt source override GSI:");
            klog_dec(gsi);
            klog_newline();
        }

        entry += hdr->length;
    }
}

void acpi_init(BootInfo* bootInfo)
{
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
}
