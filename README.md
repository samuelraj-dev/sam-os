# Sam Os - A 64-bit Kernel with UEFI Bootloader

A from-scratch x86_64 kernel built with a custom UEFI bootloader, ELF loader, Virtual Memory, Basic Graphics

## Concepts learnt:
- UEFI boot process
- Long mode
- ELF64 loading
- Memory Map & Boot services
- Virtual Memory & Page tables
- CR3 switching
- GOP & Framebuffer graphics
- CPUID
- CMOS

## Build Requirements

- Linux (tested on Kubuntu 22.04+)
- `gnu-efi` headers and libraries
- `x86_64-w64-mingw32-gcc`
- `x86_64-linux-gnu-gcc`
- `x86_64-linux-gnu-as`
- `x86_64-linux-gnu-ld`
- `qemu-system-x86_64`
- `readelf`
- `objdump`

## Installation (Ubuntu/Kubuntu)

```bash
sudo apt update && \
sudo apt install build-essential gcc gnu-efi \
    binutils \
    gcc-mingw-w64-x86-64 \
    qemu-system-x86 \
    grub-pc-bin grub-efi-amd64-bin ovmf \
    xorriso
```

## Build Steps

- Build Bootloader only: `make bootloader`
- Build Kernel only: `make kernel`
- Build all: `make all`
- Run in QEMU: `sudo make run`
- Clean build: `make clean`

## Tested On

- Lenovo ThinkPad E16 (AMD Ryzen 7530U)
- QEMU (x86_64)