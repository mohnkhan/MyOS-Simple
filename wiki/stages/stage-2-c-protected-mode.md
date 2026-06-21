[← Home](../Home.md)

# Stage 2 — A C Kernel in Protected Mode

*Getting to C is the hard part, not the C itself.*

Stage 1's entire OS lived in a single boot sector and spoke to the BIOS. Stage 2
crosses the largest gap in the whole tutorial: it loads a separate kernel from
disk, switches the CPU from 16-bit real mode into 32-bit protected mode, and
hands control to a C function. From there the kernel writes directly to video
memory and reads the keyboard by polling — because the BIOS services from Stage 1
no longer exist in protected mode.

- Directory: `helloworld-os-c/`
- Mode: 32-bit protected mode (after a real-mode bootstrap)
- Language: C + NASM
- Title on screen: `== Advanced Keyboard Demo OS ==`


## What's new vs Stage 1

| | Stage 1 | Stage 2 |
|---|---|---|
| Code that boots | one 512-byte sector | a bootloader **plus** a separate kernel loaded from disk |
| CPU mode | 16-bit real | 16-bit real → **32-bit protected** |
| Language | assembly only | a hand-written assembly bootstrap + a **C kernel** |
| Display | BIOS `int 0x10` | **direct VGA writes** to `0xB8000` |
| Keyboard | BIOS `int 0x16` | **polled 8042 controller** at ports `0x60`/`0x64` |
| Address space | 1 MiB, no protection | 4 GiB flat, protected mode |

The new machinery: a Global Descriptor Table, the `CR0.PE` protection-enable
bit, a far jump to flush the pipeline and load `CS`, a linker script that places
the kernel at a fixed address, and the freestanding C build. This article shows
how they fit together in this stage's code; each has a dedicated concept page.


## The files

| File | Role |
|------|------|
| `boot.asm` | The bootloader. Sets up segments and a stack, loads the kernel from disk, installs the GDT, enables protected mode, and calls the kernel. |
| `kernel_entry.asm` | A tiny 32-bit stub linked at `0x1000` as `_start`; it calls `kernel_main` and halts if it ever returns. |
| `kernel.c` | The C kernel: a VGA color showcase plus an advanced keyboard demo. |
| `keyboard.h` | Port numbers, status flags, the `KeyboardState` struct, and Set-1 scancode-to-ASCII tables. |
| `linker.ld` | Links the kernel to a **flat binary** whose first byte sits at `0x1000`. |
| `Makefile` | Compiles freestanding C, assembles the stubs, links, and concatenates boot sector + kernel into a disk image. |


## Code walkthrough: crossing into protected mode

The whole transition lives in `boot.asm`. It runs in real mode at `0x7C00` (just
like Stage 1) and ends in 32-bit code.

### 1. Segments, stack, and loading the kernel

```asm
start:
    mov [BOOT_DRIVE], dl    ; BIOS leaves the boot drive in DL
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    ; ... clear screen via int 0x10 ...
    mov bx, KERNEL_OFFSET   ; 0x1000
    mov dh, 16              ; load 16 sectors
    mov dl, [BOOT_DRIVE]
    call disk_load
```

The bootloader reads the kernel from disk into physical `0x1000` using BIOS
`int 0x13` with CHS addressing, starting at sector 2 (sector 1 is the boot
sector itself):

```asm
disk_load:
    mov ah, 0x02    ; BIOS: read sectors
    mov al, dh      ; count
    mov cl, 0x02    ; start at sector 2
    mov ch, 0x00    ; cylinder 0
    mov dh, 0x00    ; head 0
    int 0x13
    jc disk_error   ; carry set = error
```

It loads **16 sectors** even though the kernel is only about 3.6 KB — two
sectors would truncate it (`boot.asm:35`). The mechanics of `int 0x13` are
covered in [disk-loading-int13.md](../concepts/disk-loading-int13.md).

### 2. The Global Descriptor Table

Protected mode requires a GDT before it can be entered. This one is the minimal
flat 3-entry table used by every C stage:

```asm
gdt_start:
    dd 0x0
    dd 0x0           ; null descriptor

gdt_code:
    dw 0xffff        ; limit 0..15
    dw 0x0           ; base 0..15
    db 0x0           ; base 16..23
    db 10011010b     ; access: present, ring 0, code, readable
    db 11001111b     ; flags: 4 KiB granularity, 32-bit + limit 16..19
    db 0x0           ; base 24..31

gdt_data:
    dw 0xffff
    dw 0x0
    db 0x0
    db 10010010b     ; access: present, ring 0, data, writable
    db 11001111b
    db 0x0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1   ; limit (size - 1)
    dd gdt_start                 ; base address

CODE_SEG equ gdt_code - gdt_start   ; selector 0x08
DATA_SEG equ gdt_data - gdt_start   ; selector 0x10
```

Both the code and data segments have base 0 and limit `0xFFFFF` with 4 KiB
granularity, so each spans the full 4 GiB — a "flat" model where logical and
physical addresses coincide. The byte-by-byte meaning of these descriptors is in
[gdt-descriptor-format.md](../reference/gdt-descriptor-format.md); the theory is
in [global-descriptor-table.md](../concepts/global-descriptor-table.md).

### 3. The switch and the far jump

```asm
    cli                     ; no interrupts during the switch
    lgdt [gdt_descriptor]   ; load the GDT register
    mov eax, cr0
    or eax, 0x1             ; set CR0.PE (protection enable)
    mov cr0, eax
    jmp CODE_SEG:init_pm    ; far jump: flush pipeline, load CS
```

Setting `CR0.PE` turns on protected mode, but the CPU is still running with the
old real-mode `CS`. The **far jump** to `CODE_SEG:init_pm` is what actually loads
`CS` with the 32-bit code selector and flushes the prefetch queue of
now-stale 16-bit decoding. It is mandatory — see
[protected-mode.md](../concepts/protected-mode.md).

### 4. Landing in 32-bit code

```asm
[BITS 32]
init_pm:
    mov ax, DATA_SEG    ; reload all data selectors
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000    ; protected-mode stack
    mov esp, ebp

    call KERNEL_OFFSET  ; call the kernel at 0x1000
    jmp $
```

`[BITS 32]` switches the assembler to 32-bit encoding. The data segment
registers are reloaded with `DATA_SEG`, the stack is set to `0x90000` (below the
BIOS area, comfortably clear of the kernel), and `call 0x1000` enters the kernel.

> ⚠️ **Caveat:** Between setting `CR0.PE` and completing the far jump the CPU is
> in a half-switched state. You must not touch segment registers or run a normal
> jump there — only the far jump reloads `CS` correctly. The `cli` is equally
> important: a real-mode interrupt firing mid-switch would jump through the
> obsolete real-mode IVT.


## Code walkthrough: the C entry path

`call 0x1000` lands on the first byte of the linked kernel, which is `_start` in
`kernel_entry.asm`:

```asm
[BITS 32]
[EXTERN kernel_main]
global _start
_start:
    call kernel_main
    jmp $
```

For this to work, `_start` must be the very first byte of the kernel image. The
linker script guarantees it:

```ld
ENTRY(_start)
OUTPUT_FORMAT(binary)
SECTIONS
{
    . = 0x1000;
    .text   : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) }
}
```

`OUTPUT_FORMAT(binary)` emits a raw flat binary (not ELF), and `. = 0x1000` sets
the load address so `.text` — with `_start` at its head — begins exactly where
the bootloader loads and calls. The Makefile links `kernel_entry.o` **first** so
its `_start` wins that position. See
[linker-scripts.md](../concepts/linker-scripts.md) and
[freestanding-c.md](../concepts/freestanding-c.md).


## Code walkthrough: VGA and the keyboard from C

With the BIOS gone, the kernel writes characters straight into video memory.
Each cell is a character byte followed by an attribute byte:

```c
#define VIDEO_MEMORY 0xb8000

void putchar_at(char c, int x, int y, char attr) {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    int offset = (y * 80 + x) * 2;   // 2 bytes per cell
    video[offset]     = c;
    video[offset + 1] = attr;
}

char make_color(char bg, char fg) {
    return (bg << 4) | fg;           // attribute byte layout
}
```

The `volatile` qualifier stops the compiler from optimizing away writes to what
looks like ordinary memory but is actually the framebuffer. See
[vga-text-mode.md](../concepts/vga-text-mode.md).

Input is read by polling the 8042 keyboard controller directly. `inb` is a tiny
inline-assembly wrapper around the `in` instruction:

```c
unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

char get_key_advanced() {
    // wait until the controller has a byte for us
    while(!(inb(KEYBOARD_STATUS_PORT) & KEYBOARD_OUTPUT_FULL));
    unsigned char scancode = inb(KEYBOARD_DATA_PORT);
    // ... decode release bit, modifiers, then table lookup ...
}
```

A scancode with the high bit (`0x80`) set is a key *release*; modifier keys
(Shift/Ctrl/Alt/Caps Lock) update the global `kbd_state` rather than producing a
character, and the rest are translated through the tables in `keyboard.h`:

```c
if (kbd_state.shift_pressed || kbd_state.caps_lock) {
    ascii_char = scancode_to_ascii_shift[scancode];
} else {
    ascii_char = scancode_to_ascii[scancode];
}
```

The demo in `kernel_main` draws boxes, prints a high-contrast "Hello, World!", a
16-color palette strip along the bottom, a live modifier-status line, and an
input field. `Ctrl+Q` paints the screen red and halts. This is a *demonstration*
kernel — there is no command prompt yet. The mechanics behind the tables are in
[scancodes.md](../concepts/scancodes.md) and
[ps2-keyboard-8042.md](../concepts/ps2-keyboard-8042.md).

> 💡 **Tidbit:** Polling means the keyboard read in `get_key_advanced` *spins* in
> a `while` loop until a byte is ready, burning CPU the whole time. There are no
> interrupts wired up in any stage of this tutorial; every kernel here is a
> busy-poller. That is simple and perfectly adequate for a single-user demo.


## How to build and run

From `helloworld-os-c/`:

```sh
make            # build the C image (and a pure-asm "simple" image)
make run        # boot the C kernel in QEMU
make run-simple # boot the pure-assembly image
make debug      # boot under QEMU with a GDB stub (-s -S) on :1234
make clean      # remove build artifacts
```

The C kernel is compiled freestanding — no standard library, no startup code:

```sh
gcc -m32 -ffreestanding -nostdlib -nostdinc -fno-builtin \
    -fno-stack-protector -fno-pic -fno-pie ... -c kernel.c -o kernel.o
nasm -f elf32 kernel_entry.asm -o kernel_entry.o
ld -m elf_i386 -T linker.ld -o kernel.bin kernel_entry.o kernel.o
cat boot.bin kernel.bin > helloworld-c.img
```

The boot sector and the kernel binary are concatenated: the boot sector is the
first 512 bytes of the disk, the kernel follows in the next sectors — exactly the
sectors `boot.asm` reads back into `0x1000`. See
[toolchain-and-build.md](../reference/toolchain-and-build.md) and the
[debugging-with-gdb.md](../guides/debugging-with-gdb.md) guide.


## What it teaches

- How a GDT and the `CR0.PE` bit move the CPU into protected mode, and why the
  far jump that follows is non-negotiable.
- Why a flat 4 GiB code/data segment model makes addresses simple.
- Linking freestanding code to a fixed load address so a raw `call` reaches the
  C entry point.
- Talking to hardware directly — VGA at `0xB8000`, keyboard at `0x60`/`0x64` —
  now that no BIOS layer exists.


## Known limits

- **A demo, not a shell.** The kernel renders a fixed UI and echoes typed
  characters; there is no command interpreter yet (that arrives in Stage 3).
- **No interrupts.** Input is polled; there is no IDT and no timer.
- **No paging or memory protection.** A flat segment model with full ring-0
  access; any address is writable.
- **CHS disk loading.** The bootloader assumes the kernel occupies contiguous
  sectors starting at sector 2 and reads a fixed count (16).


## Next stage

The demo kernel becomes interactive: a real command shell with a prompt, a line
editor, a parser, and a handful of built-in commands — all without a libc.

→ [Stage 3 — An Interactive Shell](stage-3-interactive-shell.md)


## See also

- [protected-mode.md](../concepts/protected-mode.md) — `CR0.PE`, the far jump, 32-bit mode
- [global-descriptor-table.md](../concepts/global-descriptor-table.md) — segment descriptors
- [gdt-descriptor-format.md](../reference/gdt-descriptor-format.md) — the descriptor bits, byte by byte
- [disk-loading-int13.md](../concepts/disk-loading-int13.md) — reading the kernel from disk
- [freestanding-c.md](../concepts/freestanding-c.md) — C with no runtime
- [linker-scripts.md](../concepts/linker-scripts.md) — placing `.text` at `0x1000`
- [vga-text-mode.md](../concepts/vga-text-mode.md) — writing to `0xB8000`
- [ps2-keyboard-8042.md](../concepts/ps2-keyboard-8042.md) — polling the controller
- [io-ports.md](../reference/io-ports.md) — the ports used by each subsystem
- [Stage 1 — The Assembly Boot Sector](stage-1-assembly-boot.md)
- [Home](../Home.md)
