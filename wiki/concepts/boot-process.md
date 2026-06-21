[← Home](../Home.md)

# The Boot Process

*From the power button to your first line of C — how a bare-metal x86 machine wakes up and hands control to MyOS-Simple.*

This page is the **spine** of the MyOS-Simple wiki. It walks the whole journey
from power-on to the kernel's C entry point, and links out to the dedicated
pages that explain each milestone in depth. If you are new here, read this top
to bottom, then dive into whichever stop interests you most.

## The big picture

When you press the power button, no operating system exists yet. There is only
firmware burned into a ROM chip on the motherboard — the **BIOS** (Basic
Input/Output System). The BIOS knows nothing about MyOS-Simple, Linux, or
Windows. Its job is narrow: test the hardware, find a bootable device, copy the
first 512 bytes from it into memory, and jump into them. Everything after that
is *our* code.

```text
Power on
   │
   ▼
BIOS POST  ── tests CPU, RAM, devices; builds the IVT and BIOS data area
   │
   ▼
Find bootable device  ── checks each candidate's last 2 bytes for 0xAA55
   │
   ▼
Load 512 bytes to 0x7C00  ── the boot sector
   │
   ▼
Jump to 0x7C00 in 16-bit REAL MODE  ── our code starts running
   │
   ▼
(MyOS stage 1)  print + wait for key + halt        → done
(MyOS stage 2+) load kernel, enter protected mode, call C
```

> 💡 **Tidbit:** The CPU does not start executing at `0x7C00`. On reset, an x86
> begins fetching instructions from `0xFFFFFFF0` (near the top of the address
> space), where the motherboard maps the BIOS ROM. The BIOS runs first; only
> *after* it loads our boot sector does execution reach `0x7C00`.

## Step 1 — Power-On Self-Test (POST)

The first thing the firmware does is **POST**: it verifies the CPU is sane,
counts and tests RAM, initialises the chipset, and enumerates basic devices
(keyboard controller, display, disks). If something critical fails, you get the
famous diagnostic beeps and the machine never boots.

POST also populates two regions of low memory that our boot code will rely on:

- The **Interrupt Vector Table (IVT)** at physical `0x00000` — 256 four-byte
  far pointers, one per interrupt number. This is what makes BIOS services
  callable via `int` instructions.
- The **BIOS Data Area (BDA)** just above it (around `0x00400`) — scratch
  storage the firmware keeps about the hardware it found.

See the [memory map](../reference/memory-map.md) for the exact layout of low
memory at this point.

## Step 2 — Finding a bootable device

The BIOS walks its configured boot order (floppy, hard disk, USB, network, …).
For each candidate it reads the very first sector — 512 bytes — and checks the
**last two bytes**:

```text
offset 510: 0x55
offset 511: 0xAA
```

Read as a little-endian 16-bit word that is `0xAA55`, the **boot signature**. If
it matches, the BIOS declares the device bootable; if not, it moves on. This
single check is the firmware's *only* sanity test that a disk contains
something runnable.

> 💡 **Tidbit:** The `0xAA55` signature is `1010101001010101` in binary — an
> alternating bit pattern. It was chosen partly because it is unlikely to appear
> by accident in a blank or corrupted sector, and partly so the firmware could
> sanity-check the data bus by reading a known pattern.

How that signature gets onto our disk image is covered in detail on the
[boot sector](boot-sector.md) page.

## Step 3 — Loading the boot sector to 0x7C00

Once a device passes the signature check, the BIOS copies its first 512 bytes to
the fixed physical address `0x7C00` and far-jumps there. From this instant the
CPU is running *our* instructions, in **16-bit [real mode](real-mode.md)**.

Why `0x7C00` of all addresses? It is a historical accident frozen into every PC
since 1981:

> 💡 **Tidbit:** The earliest IBM PC (model 5150) shipped with as little as
> 16–32 KiB of RAM. The BIOS authors chose to load the boot sector into the
> *last* kilobyte of the first 32 KiB: `0x7C00 = 32768 − 1024`. That left all
> the low RAM below it free for the booted program and its stack, while keeping
> the loaded sector out of the BIOS's own scratch areas. The number stuck, and
> every bootloader on Earth still lives there.

## Step 4 — The boot sector takes over (real mode)

Now our code runs. What it does depends on the stage of MyOS-Simple.

### Stage 1: the whole OS *is* the boot sector

In [stage 1](../stages/stage-1-assembly-boot.md) there is no kernel and no
second stage — the entire operating system fits in 512 bytes. The monochrome
build (`helloworld-os-asm/main.asm`) sets a text video mode, positions the
cursor, prints two strings with BIOS teletype output, waits for **Q**, then
halts:

```asm
start:
    ; clear screen clrscr()
    mov ah, 0x00
    mov al, 0x03
    int 0x10
```

`helloworld-os-asm/main.asm:13-17`

That is the complete boot-to-screen path: no disk loading, no mode switch. The
[BIOS services](bios-services.md) page explains every `int 0x10` call this build
uses, and the colour variant `main_color.asm` is dissected on the
[stage 1](../stages/stage-1-assembly-boot.md) page.

### Stage 2+: the boot sector is a *bootloader*

From [stage 2](../stages/stage-2-c-protected-mode.md) onward, 512 bytes is far
too small for a C kernel, so the boot sector becomes a true **bootloader**:
its only job is to set up the machine and load the real kernel from disk. This
is the code in `helloworld-os-c/boot.asm`.

#### 4a. Save the boot drive and set up segments

```asm
start:
    ; Save boot drive number
    mov [BOOT_DRIVE], dl

    ; Set up segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
```

`helloworld-os-c/boot.asm:17-26`

The BIOS hands us the boot device number in `DL`, so we stash it before anything
clobbers it. Then we zero the segment registers (`DS = ES = SS = 0`) so that
`segment:offset` addresses are easy to reason about, and point the stack pointer
at `0x7C00`. Because the stack grows *downward*, putting `SP` at `0x7C00` means
pushes land in the free memory *below* our code, never overwriting it. (Why
segments work this way is the whole subject of the [real mode](real-mode.md)
page.)

#### 4b. Clear the screen

```asm
    ; Clear screen
    mov ah, 0x00
    mov al, 0x03
    int 0x10
```

`helloworld-os-c/boot.asm:29-31`

`AH=0`, `AL=3` selects the 80×25, 16-colour text mode and clears the display in
one BIOS call. See [BIOS services](bios-services.md).

#### 4c. Load the kernel from disk

```asm
    ; Load kernel from disk
    mov bx, KERNEL_OFFSET
    mov dh, 16          ; Load 16 sectors (kernel is ~3.6KB, needs >2)
    mov dl, [BOOT_DRIVE]
    call disk_load
```

`helloworld-os-c/boot.asm:34-37`

`KERNEL_OFFSET` is `0x1000`. The loader reads the kernel from the disk —
starting at **sector 2**, because sector 1 is the boot sector itself — into
memory at `0x1000`, using BIOS `int 0x13`. The number of sectors read grows with
the kernel from stage to stage (16 / 15 / 39 / 39). The full mechanism, the
CHS addressing scheme, and the error path live on the
[disk loading with INT 13h](disk-loading-int13.md) page.

#### 4d. Switch to 32-bit protected mode

```asm
    ; Switch to protected mode
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEG:init_pm
```

`helloworld-os-c/boot.asm:40-45`

This is the pivotal moment. We disable interrupts (`cli`), load a
**Global Descriptor Table** with `lgdt`, set the `PE` (Protection Enable) bit of
control register `CR0`, and far-jump to flush the CPU's prefetch pipeline and
load the new code selector. After the jump the CPU is in **32-bit protected
mode** — flat 4 GiB addressing, no more segment arithmetic, and *no more BIOS
services*. The descriptor table is explained on the
[Global Descriptor Table](global-descriptor-table.md) page, and the mode itself
on the [protected mode](protected-mode.md) page.

> ⚠️ **Caveat:** The moment you set `CR0.PE`, every BIOS service from
> [int 0x10/0x13/0x16](bios-services.md) becomes unavailable — they are 16-bit
> real-mode routines. That is why the boot sector clears the screen, loads the
> kernel, and reads any needed data *before* the switch. After protected mode,
> the kernel talks to hardware directly (e.g. writing the
> [VGA text buffer](vga-text-mode.md) at `0xB8000`).

## Step 5 — Into 32-bit code

```asm
[BITS 32]
init_pm:
    ; Set up protected mode segments
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp

    ; Call C kernel
    call KERNEL_OFFSET

    jmp $
```

`helloworld-os-c/boot.asm:47-63`

Now in 32-bit mode, we reload every data segment register with the **data
selector** (`DATA_SEG = 0x10`) from our GDT, then set up a fresh stack with
`EBP = ESP = 0x90000`. That address sits well above both our boot sector and the
loaded kernel, giving the stack plenty of room to grow downward. Finally we
`call 0x1000` — the address the kernel was loaded to and linked at.

## Step 6 — The kernel entry point

The first thing at `0x1000` is **not** C code; it is a tiny assembly stub,
`kernel_entry.asm`, linked at the very start of the kernel by the
[linker script](linker-scripts.md):

```asm
[BITS 32]
[EXTERN kernel_main]

global _start

_start:
    call kernel_main
    jmp $
```

`helloworld-os-c/kernel_entry.asm:8-15`

`_start` exists because we are writing
[freestanding C](freestanding-c.md) — there is no C runtime (`crt0`) to set up
`main` for us, so this stub *is* our runtime. It simply calls `kernel_main`, the
first C function, and halts forever if that ever returns.

- In **stage 2**, `kernel_main` is the program: it draws to the
  [VGA text buffer](vga-text-mode.md) and stops.
- In **stages 3–5**, `kernel_main` initialises subsystems and then enters an
  interactive [shell](../stages/stage-3-interactive-shell.md)
  (`shell_main`), which never returns.

And that is the whole journey: **firmware → boot sector → real mode → disk load
→ protected mode → C**. From here the wiki branches into the individual
subsystems the kernel builds on top.

## The path at a glance

| Stop | Where | CPU mode | Key page |
|------|-------|----------|----------|
| POST + signature check | BIOS ROM | 16-bit real | (this page) |
| Load 512 B → `0x7C00` | BIOS | 16-bit real | [boot-sector.md](boot-sector.md) |
| Segments + stack | `boot.asm:17-26` | 16-bit real | [real-mode.md](real-mode.md) |
| Clear screen | `boot.asm:29-31` | 16-bit real | [bios-services.md](bios-services.md) |
| Load kernel from disk | `boot.asm:34-37` | 16-bit real | [disk-loading-int13.md](disk-loading-int13.md) |
| Enter protected mode | `boot.asm:40-45` | → 32-bit | [protected-mode.md](protected-mode.md) |
| Reload selectors, set stack | `boot.asm:47-58` | 32-bit | [global-descriptor-table.md](global-descriptor-table.md) |
| `call 0x1000` → `_start` | `kernel_entry.asm` | 32-bit | [freestanding-c.md](freestanding-c.md) |
| `kernel_main` | `kernel.c` | 32-bit | [stage-2-c-protected-mode.md](../stages/stage-2-c-protected-mode.md) |

## See also

- [Real mode](real-mode.md) — the 16-bit world the boot sector starts in
- [BIOS services](bios-services.md) — the firmware routines available before the switch
- [The boot sector](boot-sector.md) — the 512-byte contract and `0xAA55` signature
- [Disk loading with INT 13h](disk-loading-int13.md) — how stage 2+ pulls the kernel off disk
- [Protected mode](protected-mode.md) — the 32-bit mode the kernel runs in
- [The Global Descriptor Table](global-descriptor-table.md) — what `lgdt` installs
- [Freestanding C](freestanding-c.md) — why `_start` calls `kernel_main`
- [Memory map](../reference/memory-map.md) — every fixed address used above
- [Stage 1: assembly boot](../stages/stage-1-assembly-boot.md) · [Stage 2: C in protected mode](../stages/stage-2-c-protected-mode.md)
- [Home](../Home.md)
