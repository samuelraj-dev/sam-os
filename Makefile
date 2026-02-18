CC = x86_64-w64-mingw32-gcc
KERNEL_CC = x86_64-linux-gnu-gcc
KERNEL_AS = x86_64-linux-gnu-as
KERNEL_LD = x86_64-linux-gnu-ld

CFLAGS = -ffreestanding -fshort-wchar -mno-red-zone \
         -I/usr/include/efi -I/usr/include/efi/x86_64

LDFLAGS = -nostdlib -Wl,--subsystem,10 -Wl,-entry,efi_main

KERNEL_CFLAGS = -ffreestanding -mno-red-zone -fno-stack-protector \
                -nostdlib -nostdinc -fno-builtin -Wall -Wextra \
                -fno-pic -fno-pie

KERNEL_LDFLAGS = -T scripts/linker.ld -nostdlib

BINDIR = bin
OBJDIR = build

all: bootloader kernel

bootloader: boot/bootloader.c
	$(CC) $(CFLAGS) boot/bootloader.c -o $(BINDIR)/BOOTX64.EFI $(LDFLAGS) && \
	mkdir -p target/EFI/BOOT && \
	cp $(BINDIR)/BOOTX64.EFI target/EFI/BOOT/BOOTX64.EFI

debug: debug/main_debug.c
	$(CC) $(CFLAGS) debug/main_debug.c -o $(BINDIR)/debug.efi $(LDFLAGS)

kernel: boot/boot.S kernel/kernel.c kernel/font8x8_basic.c kernel/pmm.c kernel/paging.c scripts/linker.ld
	$(KERNEL_AS) boot/boot.S -o $(OBJDIR)/boot.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/kernel.c -o $(OBJDIR)/kernel.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/font8x8_basic.c -o $(OBJDIR)/font8x8_basic.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/pmm.c -o $(OBJDIR)/pmm.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/paging.c -o $(OBJDIR)/paging.o && \
	$(KERNEL_LD) $(KERNEL_LDFLAGS) $(OBJDIR)/boot.o $(OBJDIR)/kernel.o $(OBJDIR)/font8x8_basic.o $(OBJDIR)/pmm.o $(OBJDIR)/paging.o -o $(BINDIR)/kernel.elf && \
	mkdir -p ./target && \
	cp $(BINDIR)/kernel.elf target/kernel.elf

# Debug target - shows kernel entry point
debug-kernel:
	readelf -h $(BINDIR)/kernel.elf | grep Entry
	readelf -l $(BINDIR)/kernel.elf
	objdump -d $(BINDIR)/kernel.elf | head -30

# Run with serial output for debugging
run:
	cd target && \
	qemu-system-x86_64 \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-drive if=pflash,format=raw,file=/usr/share/OVMF/OVMF_VARS_4M.fd \
		-drive format=raw,file=fat:rw:. \
		-device qemu-xhci \
		-device usb-tablet \
		-net none \
		-serial stdio \
		-m 1024

# Run with debugging enabled
run-debug:
	cd target && \
	qemu-system-x86_64 \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-drive if=pflash,format=raw,file=/usr/share/OVMF/OVMF_VARS_4M.fd \
		-drive format=raw,file=fat:rw:. \
		-device qemu-xhci \
		-device usb-tablet \
		-net none \
		-serial stdio \
		-d int,cpu_reset \
		-no-reboot \
		-no-shutdown \
		-m 1024

# usb:
# 	sudo wipefs -a /dev/sda && \
# 	sudo dd if=/dev/zero of=/dev/sda bs=1M count=10 && \

to-usb:
	sudo rm -rf /media/sam/2DA5-FCFD1/* && \
	sudo mkdir -p /media/sam/2DA5-FCFD1/EFI/BOOT && \
	sudo cp ./target/EFI/BOOT/BOOTX64.EFI /media/sam/2DA5-FCFD1/EFI/BOOT/ && \
	sudo cp ./target/kernel.elf /media/sam/2DA5-FCFD1/ && \
	sync && \
	sudo umount /media/sam/2DA5-FCFD1

to-usb-debug:
	sudo rm -rf /media/sam/2DA5-FCFD1/* && \
	sudo mkdir -p /media/sam/2DA5-FCFD1/EFI/BOOT && \
	sudo cp $(BINDIR)/debug.efi /media/sam/2DA5-FCFD1/EFI/BOOT/BOOTX64.EFI && \
	sudo cp ./target/kernel.elf /media/sam/2DA5-FCFD1/KERNEL.ELF && \
	sudo touch /media/sam/2DA5-FCFD1/HELLO.TXT && \
	sudo touch /media/sam/2DA5-FCFD1/hello2.txt && \
	sync && \
	sudo umount /media/sam/2DA5-FCFD1

clean:
	rm -f $(BINDIR)/*.elf $(BINDIR)/*.efi $(BINDIR)/*.EFI $(OBJDIR)/*.o && \
	rm -rf target

.PHONY: all clean run run-debug debug-kernel
