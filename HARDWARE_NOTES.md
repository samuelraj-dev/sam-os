# SamOS Hardware Notes

## Validation Order

1. QEMU first for every kernel foundation or hardware-discovery change.
2. ThinkPad E16 after QEMU boot, timer IRQ, keyboard IRQ, and serial log are clean.

## Current Machine Target

- Machine: Lenovo ThinkPad E16
- CPU: AMD Ryzen 7530U
- Firmware: UEFI
- Current hardware mode: discovery only. ACPI tables are parsed, but APIC/IOAPIC are not enabled yet.

## What To Record Per Real Boot

- Date and kernel commit/checkpoint.
- Whether framebuffer output appears.
- Whether serial output is available.
- Memory map entry count and unusual memory types.
- RSDP address and whether XSDT or RSDT was used.
- MADT local APIC address.
- Number of CPU local APIC entries.
- IOAPIC address and GSI base.
- Interrupt source overrides.
- Any exception vector, CR2 value, or QEMU/firmware reset behavior.

## Current Hardware Assumptions

- Legacy PIC/PIT remains the active interrupt backend.
- ACPI/MADT is discovery-only until output is trusted on QEMU and the ThinkPad.
- The kernel still keeps an identity map because PMM, VMM, ACPI, and early device code use physical pointers directly.
- Userspace remains disabled by default while foundations and hardware discovery stabilize.
