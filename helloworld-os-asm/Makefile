# Makefile for HelloWorld OS Assembly versions

# Assembler
ASM = nasm
ASM_FLAGS = -f bin

# Emulator
QEMU = qemu-system-x86_64
QEMU_FLAGS = -drive format=raw,file=

# Default target
all: helloworld.img helloworld_color.img
	@echo "Build complete!"
	@echo "Run 'make run' for original version"
	@echo "Run 'make run-color' for colorful version"

# Build original version
helloworld.img: main.asm
	$(ASM) $(ASM_FLAGS) main.asm -o helloworld.img
	@echo "Built original Hello World OS: helloworld.img"

# Build color version
helloworld_color.img: main_color.asm
	$(ASM) $(ASM_FLAGS) main_color.asm -o helloworld_color.img
	@echo "Built Colorful Hello World OS: helloworld_color.img"

# Run original version
run: helloworld.img
	@echo "Running original Hello World OS..."
	@echo "Press 'q' or 'Q' to quit"
	$(QEMU) $(QEMU_FLAGS)helloworld.img

# Run color version
run-color: helloworld_color.img
	@echo "Running Colorful Hello World OS..."
	@echo "Controls:"
	@echo "  1-9: Change background colors"
	@echo "  SPACE: Reset to default"
	@echo "  ENTER: Rainbow effect"
	@echo "  Q/q: Quit"
	$(QEMU) $(QEMU_FLAGS)helloworld_color.img

# Clean build artifacts
clean:
	rm -f *.img
	@echo "Cleaned all .img files"

# Rebuild everything
rebuild: clean all

# Help target
help:
	@echo "Available targets:"
	@echo "  make         - Build both versions"
	@echo "  make run     - Run original version"
	@echo "  make run-color - Run colorful version"
	@echo "  make clean   - Remove all .img files"
	@echo "  make rebuild - Clean and rebuild everything"
	@echo "  make help    - Show this help message"

.PHONY: all run run-color clean rebuild help
