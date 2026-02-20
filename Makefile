CC = x86_64-w64-mingw32-gcc
KERNEL_CC = x86_64-linux-gnu-gcc
KERNEL_AS = x86_64-linux-gnu-as
KERNEL_LD = x86_64-linux-gnu-ld
USER_CC     = x86_64-linux-gnu-gcc

CFLAGS = -ffreestanding -fshort-wchar -mno-red-zone \
         -I/usr/include/efi -I/usr/include/efi/x86_64

LDFLAGS = -nostdlib -Wl,--subsystem,10 -Wl,-entry,efi_main

KERNEL_CFLAGS = -ffreestanding -mno-red-zone -fno-stack-protector \
                -nostdlib -nostdinc -fno-builtin -Wall -Wextra \
                -fno-pic -fno-pie -mcmodel=kernel

KERNEL_LDFLAGS = -T scripts/linker.ld -nostdlib

USER_CFLAGS = -ffreestanding -fno-stack-protector -fno-pic \
              -fno-pie -no-pie -mno-red-zone -nostdlib -nostdinc \
              -fno-builtin -O2

BINDIR = bin
OBJDIR = build

all: bootloader.efi hello.elf hello.bin hello_blob.o kernel.elf

bootloader.efi: boot/bootloader.c
	$(CC) $(CFLAGS) boot/bootloader.c -o $(BINDIR)/BOOTX64.EFI $(LDFLAGS) && \
	mkdir -p target/EFI/BOOT && \
	cp $(BINDIR)/BOOTX64.EFI target/EFI/BOOT/BOOTX64.EFI

debug: debug/main_debug.c
	$(CC) $(CFLAGS) debug/main_debug.c -o $(BINDIR)/debug.efi $(LDFLAGS)

kernel.elf: boot/boot.S kernel/isr.S kernel/kernel.c kernel/font8x8_basic.c kernel/pmm.c kernel/paging.c kernel/display.c kernel/gdt.c kernel/tss.c kernel/idt.c kernel/pic.c kernel/timer.c kernel/keyboard.c kernel/vmm.c kernel/percpu.c kernel/syscall.c kernel/syscall_entry.S kernel/process.c scripts/linker.ld
	$(KERNEL_AS) boot/boot.S -o $(OBJDIR)/boot.o && \
	$(KERNEL_AS) kernel/isr.S -o $(OBJDIR)/isr.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/kernel.c -o $(OBJDIR)/kernel.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/font8x8_basic.c -o $(OBJDIR)/font8x8_basic.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/pmm.c -o $(OBJDIR)/pmm.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/paging.c -o $(OBJDIR)/paging.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/display.c -o $(OBJDIR)/display.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/gdt.c -o $(OBJDIR)/gdt.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/tss.c -o $(OBJDIR)/tss.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/idt.c -o $(OBJDIR)/idt.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/pic.c -o $(OBJDIR)/pic.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/timer.c -o $(OBJDIR)/timer.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/keyboard.c -o $(OBJDIR)/keyboard.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/vmm.c -o $(OBJDIR)/vmm.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/percpu.c -o $(OBJDIR)/percpu.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/syscall.c -o $(OBJDIR)/syscall.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/syscall_entry.S -o $(OBJDIR)/syscall_entry.o && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c kernel/process.c -o $(OBJDIR)/process.o && \
	$(KERNEL_LD) $(KERNEL_LDFLAGS) $(OBJDIR)/boot.o $(OBJDIR)/isr.o $(OBJDIR)/kernel.o $(OBJDIR)/font8x8_basic.o $(OBJDIR)/pmm.o $(OBJDIR)/paging.o $(OBJDIR)/display.o $(OBJDIR)/gdt.o $(OBJDIR)/tss.o $(OBJDIR)/idt.o $(OBJDIR)/pic.o $(OBJDIR)/timer.o $(OBJDIR)/keyboard.o $(OBJDIR)/vmm.o $(OBJDIR)/percpu.o $(OBJDIR)/syscall.o $(OBJDIR)/syscall_entry.o $(OBJDIR)/hello_blob.o $(OBJDIR)/process.o -o $(BINDIR)/kernel.elf && \
	mkdir -p ./target && \
	cp $(BINDIR)/kernel.elf target/kernel.elf

hello.elf: user/hello.c user/user.ld
	$(USER_CC) $(USER_CFLAGS) -T user/user.ld \
		-e _start -o user/hello.elf user/hello.c

hello.bin: user/hello.elf
	x86_64-linux-gnu-objcopy -O binary user/hello.elf user/hello.bin

hello_blob.o: user/hello.bin
	x86_64-linux-gnu-objcopy \
		-I binary -O elf64-x86-64 -B i386:x86-64 \
		user/hello.bin $(OBJDIR)/hello_blob.o
# hello_blob.o: user/hello.elf
# 	x86_64-linux-gnu-objcopy \
# 		-I binary -O elf64-x86-64 -B i386:x86-64 \
# 		user/hello.elf $(OBJDIR)/hello_blob.o

# Debug target - shows kernel entry point
debug-kernel:
	readelf -h $(BINDIR)/kernel.elf | grep Entry
	readelf -l $(BINDIR)/kernel.elf
	objdump -d $(BINDIR)/kernel.elf | head -30

# Run with serial output for debugging
# run:
# 	cd target && \
# 	qemu-system-x86_64 \
# 		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
# 		-drive if=pflash,format=raw,file=/usr/share/OVMF/OVMF_VARS_4M.fd \
# 		-drive format=raw,file=fat:rw:. \
# 		-device qemu-xhci \
# 		-device usb-tablet \
# 		-net none \
# 		-serial stdio \
# 		-m 1024
run:
	cd target && \
	cp /usr/share/OVMF/OVMF_CODE_4M.fd OVMF_CODE.fd && \
	cp /usr/share/OVMF/OVMF_VARS_4M.fd OVMF_VARS.fd && \
	qemu-system-x86_64 \
		-drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd \
		-drive if=pflash,format=raw,file=OVMF_VARS.fd \
		-drive format=raw,file=fat:rw:. \
		-device qemu-xhci \
		-device usb-tablet \
		-net none \
		-serial stdio \
		-m 1024 \
		-d int,cpu_reset \
		-no-reboot \
		-no-shutdown 2>&1 | tee qemu.log
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
	sudo rm -rf /media/sam/2DA5-FCFD4/* && \
	sudo mkdir -p /media/sam/2DA5-FCFD4/EFI/BOOT && \
	sudo cp ./target/EFI/BOOT/BOOTX64.EFI /media/sam/2DA5-FCFD4/EFI/BOOT/ && \
	sudo cp ./target/kernel.elf /media/sam/2DA5-FCFD4/ && \
	sync && \
	sudo umount /media/sam/2DA5-FCFD4

to-usb-debug:
	sudo rm -rf /media/sam/2DA5-FCFD4/* && \
	sudo mkdir -p /media/sam/2DA5-FCFD4/EFI/BOOT && \
	sudo cp $(BINDIR)/debug.efi /media/sam/2DA5-FCFD4/EFI/BOOT/BOOTX64.EFI && \
	sudo cp ./target/kernel.elf /media/sam/2DA5-FCFD4/KERNEL.ELF && \
	sudo touch /media/sam/2DA5-FCFD4/HELLO.TXT && \
	sudo touch /media/sam/2DA5-FCFD4/hello2.txt && \
	sync && \
	sudo umount /media/sam/2DA5-FCFD4

clean:
	rm -f $(BINDIR)/*.elf $(BINDIR)/*.efi $(BINDIR)/*.EFI $(OBJDIR)/*.o && \
	rm -rf target

.PHONY: all clean run run-debug debug-kernel
