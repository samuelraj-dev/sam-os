# SamOS Codebase Guide

This folder contains a small x86_64 operating system kernel. The important idea is that the bootloader prepares enough machine state to enter the kernel, and the kernel then replaces that temporary setup with its own tables, memory managers, interrupts, and device handlers.

## 1. Bootloader Flow

The bootloader lives in `boot/bootloader.c` and is built as `BOOTX64.EFI`.

1. UEFI starts `efi_main`.
2. The bootloader opens the EFI filesystem and reads `kernel.elf`.
3. It validates the ELF header and loads every `PT_LOAD` segment to the physical address requested by the ELF program headers.
4. It asks GOP for the framebuffer address, screen width, height, and pitch.
5. It builds early page tables:
   - An identity map so physical addresses can be used directly.
   - A higher-half mapping so the kernel can run at `0xFFFFFFFF80000000 + physical`.
6. It asks UEFI for the memory map.
7. It calls `ExitBootServices`. After this point, UEFI boot services are gone and the kernel owns the machine.
8. It loads CR3 with the bootloader page table address and jumps to the kernel entry point with a `BootInfo*` in `rdi`.

`BootInfo` is the handoff contract between bootloader and kernel. It contains the framebuffer information and the UEFI memory map.

## 2. Kernel Entry And Startup Order

Assembly starts in `boot/boot.S`. It disables interrupts, aligns the stack, and calls `kernel_main(BootInfo*)` in `kernel/kernel.c`.

The startup order matters:

1. Initialize display first, so failures can be printed.
2. Load GDT, TSS, and IDT, so CPU exceptions and interrupts have valid tables.
3. Initialize PIC, PIT timer, and keyboard while interrupts are still disabled.
4. Build kernel-owned paging and switch CR3.
5. Reload GDT/TSS after the CR3 switch.
6. Initialize PMM, then allocate the display shadow buffer.
7. Initialize VMM and run memory smoke tests.
8. Unmask timer/keyboard IRQs and enable interrupts with `sti`.

The default boot currently stops at a stable kernel baseline. Userspace launch is preserved in the codebase, but it is not enabled by default yet.

## 3. Display

`kernel/display.c` owns text output.

Before PMM is ready, text is drawn directly into the framebuffer. After PMM is ready, the kernel allocates a shadow buffer and calls `display_set_shadow`. From then on, drawing updates the shadow buffer and marks dirty rows. `display_flush()` copies dirty rows to the real framebuffer.

This keeps interrupt handlers simple: keyboard and timer code can request output, while the main loop flushes safely outside the IRQ path.

## 3.5 Logging And Serial

`kernel/serial.c` initializes COM1 and writes characters to the serial port. In QEMU, this is connected to `-serial stdio`, so kernel logs survive even if framebuffer drawing breaks.

`kernel/klog.c` provides simple log levels: `INFO`, `WARN`, `ERROR`, and `PANIC`. Early boot still uses some direct `print` calls, but new subsystem work should prefer `klog_*` so messages appear on both the framebuffer and serial output.

## 4. Descriptor Tables And Interrupts

`kernel/gdt.c` builds segment descriptors. Long mode mostly ignores segmentation, but code/data selectors are still required for interrupts and ring transitions.

`kernel/tss.c` builds the TSS. The TSS gives the CPU a known kernel stack for privilege transitions and an IST stack for double faults.

`kernel/idt.c` installs exception and IRQ handlers. CPU exceptions print useful state such as vector, error code, RIP, RSP, RFLAGS, and CR2 for page faults.

`kernel/isr.S` contains the raw assembly stubs. Each stub pushes a vector number and error code shape, saves registers, calls `interrupt_handler`, restores registers, and returns with `iretq`.

`kernel/pic.c` remaps the legacy PIC so IRQs start at vector `0x20`. The top-level interrupt handler owns PIC EOI, so individual timer/keyboard handlers do not send EOI themselves.

`kernel/irq.c` is the interrupt-controller front door. It currently delegates to the legacy PIC backend, but later APIC/IOAPIC work should plug in here instead of changing device drivers.

## 5. Timer And Keyboard

`kernel/timer.c` programs the PIT and counts timer ticks. The timer interrupt only increments a counter and marks display flushing as needed.

`kernel/keyboard.c` configures the PS/2 controller and handles IRQ1. It currently translates simple scancodes through `scancode_map` and prints characters.

The main loop sleeps with `hlt`, wakes on interrupts, flushes display rows, and prints a periodic tick message.

## 6. Paging, PMM, And VMM

`kernel/paging.c` builds the kernel-owned page tables. It keeps an identity map for early physical-memory access and maps the kernel in the higher half. It also enables NX support through EFER.NXE.

`kernel/pmm.c` is the physical memory manager. It reads the UEFI memory map, builds a bitmap, marks usable conventional memory as free, then reserves:

- Page zero and low memory.
- The kernel image.
- Early page tables.
- The PMM bitmap itself.

`pmm_alloc_page()` returns one physical page. `pmm_alloc_pages(count)` returns contiguous physical pages. `pmm_free_page()` marks a page free again.

`kernel/vmm.c` is the virtual memory manager. It wraps CR3 in an `AddressSpace`, creates page-table levels on demand, maps virtual pages to physical pages, unmaps pages, switches CR3, and resolves virtual addresses for debugging/loading.

During stabilization, VMM rejects unaligned mappings and remapping an already-present page. That catches many page-table bugs early.

`kernel/kmalloc.c` is the first kernel heap. It is page-backed by PMM, 16-byte aligned, and supports `kmalloc`, `kzalloc`, and `kfree`. It is intentionally simple so ACPI and later kernel structures can allocate small objects without requesting whole physical pages.

PMM currently only frees UEFI conventional memory. It explicitly reserves low memory, the kernel image, page tables, the PMM bitmap, framebuffer pages, ACPI/runtime/MMIO descriptors, and the RSDP page.

## 7. Panic And Assertions

`kernel/panic.c` provides:

- `panic(message)`: disables interrupts, prints the message, flushes display, and halts.
- `kassert(condition, message)`: calls `panic` if a boot-critical invariant fails.

Use these for impossible kernel states. They make failures visible instead of silently freezing.

## 8. Userspace Status

The codebase already contains early userspace pieces:

- `kernel/process.c`: ELF loader and user stack setup.
- `kernel/syscall.c`: syscall MSR setup and syscall handling.
- `kernel/syscall_entry.S`: assembly entry/exit path for `syscall`/`sysretq`.
- `user/hello.c`: tiny user program.

This path is not the default boot path right now. Before enabling it again, the next stabilization milestone should verify:

- User address spaces do not conflict with the kernel identity map.
- Syscall CR3 switching always uses valid per-CPU state.
- Interrupts from ring 3 land on a valid kernel stack.
- Process exit returns control to a scheduler or kernel task instead of halting forever.

## 8.5 ACPI And Hardware Discovery

The bootloader passes the ACPI RSDP pointer through `BootInfo`. `kernel/acpi.c` validates the RSDP, chooses XSDT when available, lists discovered ACPI tables, and parses MADT/APIC entries.

This is discovery-only right now. The kernel prints local APIC, IOAPIC, and interrupt source override information, but the active interrupt controller is still the legacy PIC.

## 9. How To Build And Inspect

Useful commands from `02/`:

```bash
make clean
make all
make debug-kernel
make check-build
make run
```

`make run` writes QEMU debug output to `target/qemu.log`. If the kernel crashes, inspect the exception printout on screen and the QEMU log together.
