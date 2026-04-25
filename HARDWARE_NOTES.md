# SamOS Hardware Notes

## Validation Order

1. QEMU first for every kernel foundation or hardware-discovery change.
2. ThinkPad E16 after QEMU boot, timer IRQ, keyboard IRQ, and serial log are clean.

## Current Machine Target

- Machine: Lenovo ThinkPad E16
- CPU: AMD Ryzen 7530U
- Firmware: UEFI
- Current hardware mode: APIC/IOAPIC enabled baseline with PIC fallback path retained.

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

- APIC/IOAPIC is the default interrupt backend after MADT parse and route setup.
- Legacy PIC remains as fallback when APIC enable/routing fails.
- The kernel still keeps an identity map because PMM, VMM, ACPI, and early device code use physical pointers directly.
- Userspace multiprogram boot is enabled for baseline validation (`hello` + `burn`).

## Validation Matrix (Per Change)

1. QEMU: boot banner shows APIC mode and route map, shell input works, timer ticks advance, `irqstat` counters increase.
2. QEMU: `spawnh`/`spawnb`, `ps`, `kill`, `wait`, `schedcheck`, and `memstat` pass without panic.
3. ThinkPad E16: repeat boot + keyboard + timer + shell command smoke checks.
4. Record anomalies with vector, CR2, PID/TID, backend mode, and IRQ/GSI mapping.
