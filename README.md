# MyOS-Simple

A progressive, bare-metal x86 operating-system tutorial in five self-contained
stages. Each stage is a complete, bootable image; each one adds exactly one
layer of capability on top of the previous. Read them in order and you watch an
operating system assemble itself from a single 512-byte boot sector into a
protected-mode C system with a command shell, a real-time clock, a cooperative
process model, and a floating-point calculator.

Nothing here depends on an existing OS at runtime. Every image boots on the bare
machine (or QEMU) with only the BIOS beneath it.


## The five stages

| # | Directory | Mode | Language | Lines (core) | Capability introduced |
|---|-----------|------|----------|--------------|-----------------------|
| 1 | `helloworld-os-asm`   | 16-bit real        | NASM      | 60 / 183 | Boot, print via BIOS, color + keyboard variant |
| 2 | `helloworld-os-c`     | 32-bit protected   | C + NASM  | 362 C    | Bootloader, GDT, real-to-protected switch, C kernel, direct VGA |
| 3 | `os-c-with-shell`     | 32-bit protected   | C + NASM  | 298 C    | Port-polled keyboard, interactive command shell (5 commands) |
| 4 | `helloworld-os-c-v2`  | 32-bit protected   | C + NASM  | ~2150 C  | RTC/CMOS clock, process model, FPU calculator, tab-completion, aliases (20 commands) |
| 5 | `helloworld-os-c-v3`  | 32-bit protected   | C + NASM  | ~1950 C  | Consolidated command set (18 commands), committed build artifacts |

The complexity gradient runs from 60 lines of assembly that print a string to a
~2,000-line C system that tells the time, runs cooperative tasks, and evaluates
expressions — all without a standard library.


## Design rationale

Each stage exists to answer the question the previous stage raised:

1. **What is actually underneath a "Hello, World"?** Strip away the runtime
   entirely. The BIOS loads the first 512 bytes of the disk to physical address
   `0x7C00`, executes them in 16-bit real mode, and the only services available
   are BIOS interrupts. The whole program fits in the boot sector and ends with
   the `0xAA55` signature the firmware checks for.
2. **How do we write the OS in a language we can grow?** Getting to C is the
   hard part, not the C itself: a hand-written bootloader installs a Global
   Descriptor Table, sets the protection-enable bit in `CR0`, far-jumps into
   32-bit protected mode, and calls a C entry point linked at a fixed address.
3. **How do we make the kernel interactive?** Read the keyboard controller
   directly (polling I/O ports `0x60`/`0x64`), decode scancodes, and dispatch a
   small command interpreter. There is no `libc`, so string handling and the
   prompt are written by hand.
4. **What does an operating system actually do?** Read wall-clock time from the
   CMOS RTC, model processes with a control block and a scheduler, perform
   floating-point arithmetic without a math library, and make the shell
   ergonomic (history, tab-completion, aliases).
5. **How do we stabilize and ship?** Lock the command surface, drop the
   experimental detours, and commit the actual built binaries so the artifact
   that boots is the artifact in the repository.


## Prerequisites

The build is a freestanding 32-bit (`i386`) toolchain plus an emulator. On a
64-bit host you need the 32-bit multilib support for GCC.

Debian / Ubuntu:

```sh
sudo apt update
sudo apt install nasm gcc gcc-multilib binutils make qemu-system-x86
```

Fedora:

```sh
sudo dnf install nasm gcc glibc-devel.i686 libgcc.i686 binutils make qemu-system-x86
```

Arch:

```sh
sudo pacman -S nasm gcc lib32-glibc binutils make qemu-system-x86
```

Component roles:

- **nasm** — assembles boot sectors (`-f bin`, flat binary) and the 32-bit
  kernel entry stub (`-f elf32`).
- **gcc** — compiles the C kernel as freestanding 32-bit code (`-m32`).
- **ld** (binutils) — links the kernel to a fixed load address via `linker.ld`
  (`-m elf_i386`).
- **make** — drives each stage's build.
- **coreutils** — `cat` concatenates boot sector + kernel; `truncate` pads the
  image to a whole number of sectors.
- **qemu-system-x86_64** — boots the raw disk image.


## Quick start

Each directory is independent and ships its own `Makefile` with identical verbs:

```sh
cd helloworld-os-asm     # or any other stage
make                     # build the bootable image(s)
make run                 # boot the primary image in QEMU
make clean               # remove build artifacts
make help                # list available targets
```

The C stages also provide:

```sh
make run-simple          # boot the pure-assembly "simple" image
make debug               # boot under QEMU with a GDB stub (-s -S) on :1234
```

To build every stage in one pass:

```sh
for d in helloworld-os-asm helloworld-os-c os-c-with-shell \
         helloworld-os-c-v2 helloworld-os-c-v3; do
    make -C "$d" clean && make -C "$d"
done
```


## Repository layout

```
MyOS-Simple/
├── README.md                 This file.
├── helloworld-os-asm/        Stage 1 — pure assembly.
├── helloworld-os-c/          Stage 2 — C kernel in protected mode.
├── os-c-with-shell/          Stage 3 — interactive shell.
├── helloworld-os-c-v2/       Stage 4 — clock, processes, calculator.
└── helloworld-os-c-v3/       Stage 5 — stabilized release.
```

Each stage directory contains its own `README.md` documenting that stage in
detail. Stages 4 and 5 additionally ship `README_SHELL.md`, a complete
command reference for the shell.


## Conventions shared across the C stages

These hold for stages 2–5 and are documented once here rather than repeated:

- **Load address.** The bootloader loads the kernel to physical `0x1000` and the
  linker script places `.text` at `0x1000`, so execution begins at the first
  byte of the kernel image.
- **Stack.** Protected-mode `ESP`/`EBP` are initialized to `0x90000`, comfortably
  below the kernel and above the BIOS data area.
- **GDT.** A minimal three-entry table: a null descriptor, a flat ring-0 code
  segment, and a flat ring-0 data segment, each spanning the full 4 GiB with
  4 KiB granularity (`0xCF` flags). No paging is enabled.
- **Display.** Text is written directly to VGA memory at `0xB8000`; each cell is
  a character byte followed by an attribute byte (`background << 4 | foreground`).
- **Keyboard.** Input is read by polling the 8042 controller: status port
  `0x64`, data port `0x60`. Scancodes are translated through the tables in
  `keyboard.h`.
- **Freestanding C.** Compiled with `-ffreestanding -nostdlib -nostdinc
  -fno-builtin -fno-stack-protector -fno-pic -fno-pie`. There is no C runtime;
  the assembly entry stub calls the kernel's C entry point directly.


## Known limitations

- Real-mode disk loading uses BIOS `int 0x13` with CHS addressing and assumes
  the kernel occupies contiguous sectors starting at sector 2 of the boot media.
- The process model (stage 4+) is cooperative: tasks must call `process_yield()`.
  There is no timer-driven preemption and no memory protection between tasks.
- RTC reads are not synchronized against the update-in-progress flag, so a read
  that races a CMOS update may momentarily return an inconsistent field.
- Images are sized for QEMU; running on physical hardware may require adjusting
  the sector count loaded by the bootloader.
