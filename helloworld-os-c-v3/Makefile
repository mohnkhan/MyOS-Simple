# Makefile for HelloWorld OS in C

# Compiler and assembler settings
CC = gcc
AS = nasm
LD = ld

# Flags
CFLAGS = -m32 -ffreestanding -fno-pic -fno-pie -nostdlib -nostdinc \
         -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs \
         -Wall -Wextra -c
ASFLAGS = -f bin
LDFLAGS = -m elf_i386 -T linker.ld

# Target files
BOOT = boot.bin
KERNEL = kernel.bin
IMAGE = helloworld-c.img
SIMPLE_IMAGE = helloworld-simple.img

# Build targets
all: $(IMAGE) $(SIMPLE_IMAGE)

# Build the boot sector
boot.bin: boot.asm
	$(AS) $(ASFLAGS) boot.asm -o boot.bin

# Compile kernel entry
kernel_entry.o: kernel_entry.asm
	nasm -f elf32 kernel_entry.asm -o kernel_entry.o

# Compile the shell (replacing kernel)
shell.o: shell.c
	$(CC) $(CFLAGS) shell.c -o shell.o

# Compile process management
process.o: process.c
	$(CC) $(CFLAGS) process.c -o process.o

# Compile RTC module
rtc.o: rtc.c
	$(CC) $(CFLAGS) rtc.c -o rtc.o

# Link the kernel with shell, process management, and RTC
kernel.bin: kernel_entry.o shell.o process.o rtc.o
	$(LD) $(LDFLAGS) -o kernel.bin kernel_entry.o shell.o process.o rtc.o

# Create the final disk image
$(IMAGE): boot.bin kernel.bin
	cat boot.bin kernel.bin > $(IMAGE)
	# Pad the image to ensure it's large enough (40 sectors = 20480 bytes for RTC)
	truncate -s 20480 $(IMAGE)

# Build simple version
$(SIMPLE_IMAGE): boot_simple.asm
	nasm -f bin boot_simple.asm -o $(SIMPLE_IMAGE)

# Run C version with QEMU
run: $(IMAGE)
	qemu-system-x86_64 -drive format=raw,file=$(IMAGE)

# Run simple version with QEMU
run-simple: $(SIMPLE_IMAGE)
	qemu-system-x86_64 -drive format=raw,file=$(SIMPLE_IMAGE)

# Run with QEMU in debug mode
debug: $(IMAGE)
	qemu-system-x86_64 -drive format=raw,file=$(IMAGE) -s -S

# Clean build files
clean:
	rm -f *.bin *.o *.img

# Phony targets
.PHONY: all run debug clean

help:
	@echo "Available targets:"
	@echo "  make           - Build both versions"
	@echo "  make run       - Run the C version with QEMU"
	@echo "  make run-simple - Run the simple version with QEMU"
	@echo "  make debug     - Run C version with QEMU in debug mode"
	@echo "  make clean     - Remove all build files"
	@echo "  make help      - Show this help message"
