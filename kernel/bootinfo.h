#ifndef BOOTINFO_H
#define BOOTINFO_H

#include "types.h"

typedef struct {
    void* framebuffer; // physical address of the graphics framebuffer
    uint32_t width; // Eg: 1920
    uint32_t height; // Eg: 1200
    uint32_t pixels_per_scanline; // how many pixels exist in one row in memory (not always equal to width). Typical Eg: 2048
    void* memory_map; // comes from UEFI fn GetMemoryMap() - pointer to UEFI memory map array. it points to multiple EFI_MEMORY_DESCRIPTOR structures.
    uint64_t memory_map_size; // total size of memory map buffer. Typical Eg: 4096
    uint64_t memory_map_descriptor_size; // size of one descriptor entry. Eg: sizeof(EFI_MEMORY_DESCRIPTOR) ≈ 48 bytes
    void* rsdp; // ACPI Root System Description Pointer from UEFI configuration tables.
    // No. of entries = memory_map_size / memory_map_descriptor_size
    //      EG: entries = 4096 / 48 ≈ 86 entries
} BootInfo;

// EFI_MEMORY_DESCRIPTOR structure
//
// typedef struct {
//     uint32_t Type;
//     uint32_t Pad;
//     uint64_t PhysicalStart;
//     uint64_t VirtualStart;
//     uint64_t NumberOfPages;
//     uint64_t Attribute;
// } EFI_MEMORY_DESCRIPTOR;


// Types of RAM
// EFI_CONVENTIONAL_MEMORY - only RAM OS should use initially.
// EFI_RESERVED_MEMORY_TYPE - Used by firmware or hardware. We should not touch it.
// EFI_BOOT_SERVICES_CODE - Used by UEFI boot services. After calling ExitBootService(), These usually become usable RAM.
// EFI_BOOT_SERVICES_DATA - Same as EFI_BOOT_SERVICES_CODE.
// EFI_RUNTIME_SERVICES_CODE - Used by firmware even after boot. Must remain mapped.
// EFI_RUNTIME_SERVICES_DATA - Same as EFI_RUNTIME_SERVICES_CODE.
// EFI_ACPI_RECLAIM_MEMORY - Contains ACPI tables. Needed for power mgmt.
// EFI_ACPI_MEMORY_NVS - Same as EFI_ACPI_RECLAIM_MEMORY
// EFI_MEMORY_MAPPED_IO - Used for devices like GPU, PCI devices, APCI, etc
// EFI_UNUSABLE_MEMORY - Bad RAM.


// Range of Descriptor
//
// Standard page size: 4 KB = 4096 bytes
//
// let PhysicalStart = 0x00100000 (1MiB)
// let NumberOfPages = 256
//
// Size = NumberOfPages * PageSize
// Size = 256 * 4096 => 1,048,576 => 0x00100000 (1MiB)
// Range = 0x00100000 to 0x00200000 (1MiB to 2MiB)


// Attributes in EFI_MEMORY_DESCRIPTOR
//
// EFI_MEMORY_WB   (write-back cacheable)
// EFI_MEMORY_UC   (uncacheable)
// EFI_MEMORY_WC   (write combining)
// EFI_MEMORY_RUNTIME
//
// Example:
// framebuffer → often write-combining
// RAM → write-back

#endif
