# Stage 2 — Hello World OS in C (32-bit protected mode)

The same idea as stage 1, rebuilt around a real toolchain. A hand-written
bootloader switches the CPU from 16-bit real mode into 32-bit protected mode and
hands control to a kernel written in C. The kernel writes directly to VGA memory
to produce a colorful display and reads the keyboard through scancodes.

Two independent images are produced:

- **`helloworld-c.img`** — bootloader + C kernel, the focus of this stage.
- **`helloworld-simple.img`** — a self-contained 512-byte assembly image
  (`boot_simple.asm`), kept alongside so you can compare raw BIOS output against
  VGA writes from C.


## Position in the series

Stage 1 (`helloworld-os-asm`) ran entirely in real mode using BIOS interrupts.
This stage introduces everything needed to leave real mode behind: a GDT, the
protected-mode switch, a linker script, a C entry stub, and direct hardware
access. Stage 3 (`os-c-with-shell`) builds an interactive shell on top of this
foundation.


## Boot and execution flow

```
BIOS
 └─ loads sector 0 (boot.bin) to 0x7C00, executes in 16-bit real mode
     └─ boot.asm:
         1. save boot drive, set up segments, stack at 0x7C00
         2. clear screen (int 0x10, mode 0x03)
         3. load 2 sectors from disk to 0x1000 (int 0x13, CHS)
         4. lgdt [gdt_descriptor]; set CR0.PE; far jump to flush pipeline
         └─ 32-bit protected mode (init_pm):
             5. reload segment registers from the flat data descriptor
             6. set EBP/ESP = 0x90000
             7. call 0x1000  ──► kernel_entry.asm (_start) ──► kernel_main()
```

### Memory map

| Address | Contents |
|---------|----------|
| `0x00000`–`0x004FF` | Real-mode IVT / BIOS data area |
| `0x01000` | Kernel image (`.text` starts here per `linker.ld`) |
| `0x07C00` | Boot sector (loaded by BIOS); real-mode stack grows down from here |
| `0x90000` | Protected-mode stack (`ESP`/`EBP`) |
| `0xB8000` | VGA text framebuffer (80×25, 2 bytes/cell) |

### The GDT

`boot.asm` defines a three-entry Global Descriptor Table:

| Selector | Type | Base | Limit | Flags |
|----------|------|------|-------|-------|
| `0x00` | null | — | — | — |
| `0x08` (`CODE_SEG`) | ring-0 code, executable/readable | `0x00000000` | `0xFFFFF` | `0xCF` (32-bit, 4 KiB granularity) |
| `0x10` (`DATA_SEG`) | ring-0 data, writable | `0x00000000` | `0xFFFFF` | `0xCF` |

Both segments are flat: base 0, limit 4 GiB. This is the simplest model that
lets C code use ordinary 32-bit near pointers. Paging is not enabled.

### The protected-mode switch

```asm
cli                      ; interrupts off — no IDT exists yet
lgdt [gdt_descriptor]    ; load the GDT
mov eax, cr0
or  eax, 0x1             ; set PE (protection enable)
mov cr0, eax
jmp CODE_SEG:init_pm     ; far jump reloads CS and flushes the prefetch queue
```

The far jump is mandatory: it is what actually loads `CS` with the 32-bit code
selector and begins executing 32-bit instructions.


## The C kernel (`kernel.c`)

Compiled freestanding (no libc), the kernel:

- Defines the 16 VGA colors and a `make_color(bg, fg)` helper that packs an
  attribute byte.
- Writes characters as `(char, attribute)` pairs directly into the framebuffer
  at `0xB8000`, computing the cell offset as `(row * 80 + col) * 2`.
- Renders a color showcase — titled banner, colored text, decorative boxes —
  demonstrating that C can drive the display with no BIOS help.
- Reads the keyboard via scancodes (`keyboard.h`) for interaction.

Because there is no standard library, there is no `printf`, no `malloc`, and no
startup code. The assembly stub `kernel_entry.asm` is the entry point and simply
calls `kernel_main`.


## Source layout

```
helloworld-os-c/
├── boot.asm           16-bit bootloader: load kernel, GDT, switch to PM.
├── boot_simple.asm    Standalone 512-byte assembly image (the "simple" build).
├── kernel.c           C kernel: VGA color demo + keyboard handling.
├── kernel_entry.asm   32-bit entry stub: _start -> kernel_main.
├── linker.ld          Places the kernel at 0x1000, output flat binary.
├── Makefile           Build/run/debug targets.
├── boot.bin           Assembled boot sector (build output).
├── kernel.bin         Linked kernel (build output).
├── helloworld-c.img       Final C image: boot.bin + kernel.bin, padded.
└── helloworld-simple.img  Final simple image (build output).
```


## Prerequisites

A 32-bit-capable GCC toolchain plus NASM and QEMU:

```sh
sudo apt install nasm gcc gcc-multilib binutils make qemu-system-x86
```

`gcc-multilib` is required on 64-bit hosts for `-m32`.


## Build

```sh
make            # build both helloworld-c.img and helloworld-simple.img
make clean      # remove *.bin *.o *.img
```

The build performs these steps:

```sh
# 1. Assemble the boot sector as a flat binary.
nasm -f bin boot.asm -o boot.bin

# 2. Assemble the 32-bit C entry stub as an ELF object.
nasm -f elf32 kernel_entry.asm -o kernel_entry.o

# 3. Compile the kernel, freestanding 32-bit.
gcc -m32 -ffreestanding -fno-pic -fno-pie -nostdlib -nostdinc \
    -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs \
    -Wall -Wextra -c kernel.c -o kernel.o

# 4. Link to a flat binary loaded at 0x1000.
ld -m elf_i386 -T linker.ld -o kernel.bin kernel_entry.o kernel.o

# 5. Concatenate boot sector + kernel and pad to a fixed size.
cat boot.bin kernel.bin > helloworld-c.img
truncate -s 10240 helloworld-c.img

# Simple image is a single flat binary.
nasm -f bin boot_simple.asm -o helloworld-simple.img
```

### Compiler flags explained

| Flag | Reason |
|------|--------|
| `-m32` | Generate 32-bit i386 code (the CPU is in 32-bit protected mode). |
| `-ffreestanding` | No hosted environment; `main` is not special, no libc assumptions. |
| `-nostdlib -nodefaultlibs -nostartfiles` | Do not link any C runtime or CRT startup. |
| `-nostdinc` | Do not search system headers; only the local freestanding headers. |
| `-fno-builtin` | Do not assume libc semantics for functions like `memcpy`. |
| `-fno-stack-protector` | No stack canaries (there is no runtime to support them). |
| `-fno-pic -fno-pie` | Absolute addressing; the kernel runs at a fixed load address. |
| `-Wall -Wextra` | Maximum diagnostics. |

### Linker script

`linker.ld` sets `ENTRY(_start)`, forces `OUTPUT_FORMAT(binary)` (a flat image,
not ELF), and places sections starting at `0x1000` in the order
`.text`, `.rodata`, `.data`, `.bss`. This must match the address the bootloader
loads to and calls.

### Why the image is padded

The bootloader reads a fixed number of sectors (2 here) with BIOS `int 0x13`.
`truncate -s 10240` pads the image to 20 sectors so the disk is large enough for
QEMU to read without error, even though the kernel itself is smaller.


## Run

```sh
make run          # boot helloworld-c.img (the C kernel)
make run-simple   # boot helloworld-simple.img (pure assembly)
make debug        # boot under QEMU with a GDB stub (-s -S)
```

`make debug` starts QEMU halted with a GDB server on TCP `1234`. Attach with:

```sh
gdb -ex 'target remote localhost:1234' -ex 'set architecture i386'
```


## Notes and limitations

- No IDT is installed, so hardware/CPU exceptions are not handled; a fault
  triple-resets the machine. Interrupts remain masked.
- Disk loading uses CHS addressing and assumes the kernel is contiguous from
  sector 2.
- The kernel polls the keyboard; there is no interrupt-driven input yet.
