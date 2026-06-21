[← Home](../Home.md)

# Glossary

*Concise definitions of every term used across the MyOS-Simple wiki, each linked to its concept page.*

Alphabetical where practical, otherwise grouped by topic. Each entry is one to three
sentences; follow the link for the full treatment.

## A

<a id="a20-line"></a>
**A20 line** — The 21st address line (bit 20). On the original PC it was forced low for
8086 compatibility, wrapping addresses at 1 MiB. It must be enabled to use memory above
1 MiB in [protected mode](../concepts/protected-mode.md); modern firmware/QEMU enable it
before the OS runs.

<a id="attribute-byte"></a>
**Attribute byte** — The second byte of each [VGA text cell](../concepts/vga-text-mode.md),
encoding `background << 4 | foreground` (4-bit color each). It sets the color of the
character byte beside it.

## B

<a id="bcd"></a>
**BCD (Binary-Coded Decimal)** — Each decimal digit stored in its own nibble, so 59
decimal is `0x59`, not `0x3B`. The [CMOS RTC](../concepts/cmos-rtc.md) returns time
fields in BCD by default; the kernel converts with `((bcd >> 4) * 10) + (bcd & 0x0F)`.

<a id="bios"></a>
**BIOS** — The firmware that runs at power-on, performs [POST](#post), and loads the
[boot sector](../concepts/boot-sector.md). It provides [real-mode services](../concepts/bios-services.md)
(`int 0x10`, `int 0x13`, `int 0x16`) that vanish once in protected mode.

<a id="boot-sector"></a>
**Boot sector** — The first 512-byte sector of the boot disk, loaded by the BIOS to
`0x7C00` and ending in the [`0xAA55`](#aa55-boot-signature) signature. See
[boot sector](../concepts/boot-sector.md).

<a id="aa55-boot-signature"></a>
**0xAA55 boot signature** — The two-byte marker the BIOS checks at offset 510–511 of the
boot sector. Stored little-endian as the bytes `55 AA`. Its absence means "not bootable."

## C

<a id="chs-addressing"></a>
**CHS addressing** — Cylinder-Head-Sector disk geometry used by [`int 0x13`](../concepts/disk-loading-int13.md).
Sectors are **1-based**, so the boot sector is sector 1 and the kernel begins at sector 2.

<a id="cmos-rtc"></a>
**CMOS / RTC** — Battery-backed CMOS RAM containing the real-time clock. Read via ports
`0x70` (register select) and `0x71` (data). See [CMOS / RTC](../concepts/cmos-rtc.md).

<a id="context-switch"></a>
**Context switch** — Saving one task's CPU state (registers, stack) and restoring
another's, so execution resumes where it left off. The basis of
[multitasking](#cooperative-vs-preemptive).

<a id="cooperative-vs-preemptive"></a>
**Cooperative vs preemptive multitasking** — Cooperative: a task runs until it voluntarily
yields (`process_yield()`); preemptive: a timer interrupt forcibly switches tasks.
MyOS-Simple is purely cooperative — see [cooperative scheduling](../concepts/cooperative-scheduling.md).

<a id="cr0-pe"></a>
**CR0 / PE bit** — Control register 0; setting bit 0 (the Protection Enable bit) switches
the CPU into [protected mode](../concepts/protected-mode.md). MyOS-Simple does
`or eax, 0x1; mov cr0, eax`.

<a id="crt0-startup"></a>
**crt0 / startup** — The C runtime startup object that normally sets up the stack, zeroes
`.bss`, and calls `main`. A [freestanding](../concepts/freestanding-c.md) build omits it
(`-nostartfiles`); a hand-written assembly stub jumps to the kernel entry instead.

## F

<a id="far-jump"></a>
**Far jump** — A jump that reloads `CS` along with `EIP`. After setting [CR0.PE](#cr0-pe),
`jmp CODE_SEG:init_pm` flushes the prefetch pipeline and loads the new code
[selector](#selector), completing the switch to [protected mode](../concepts/protected-mode.md).

<a id="fixed-point-arithmetic"></a>
**Fixed-point arithmetic** — Representing fractions with scaled integers instead of an FPU.
The [`calc`](command-reference.md) command scales by 1000 (three decimals). See
[fixed-point arithmetic](../concepts/fixed-point-arithmetic.md).

<a id="flat-binary"></a>
**Flat binary** — A raw output file with no headers, just the bytes to load and run. The
boot sector is built `nasm -f bin`; the BIOS executes it directly. See
[toolchain-and-build](toolchain-and-build.md).

<a id="flat-memory-model"></a>
**Flat memory model** — All segments span the whole 4 GiB at base 0, so a pointer equals a
physical address. Established by the [GDT](../concepts/global-descriptor-table.md) entries
with limit `0xFFFFF` and 4 KiB granularity.

<a id="framebuffer"></a>
**Framebuffer** — A memory region whose contents the display hardware renders. The VGA text
framebuffer is at [`0xB8000`](memory-map.md); writing it changes the screen — see
[VGA text mode](../concepts/vga-text-mode.md).

<a id="freestanding-c"></a>
**Freestanding C** — C compiled without a hosted runtime or standard library; `main` has no
special meaning and only the language core is assumed. See
[freestanding C](../concepts/freestanding-c.md).

## G

<a id="gdt"></a>
**GDT (Global Descriptor Table)** — The table of [segment descriptors](#segment-descriptor)
the CPU consults in protected mode. MyOS-Simple installs three entries (null, code, data) —
see [GDT](../concepts/global-descriptor-table.md) and the
[descriptor format reference](gdt-descriptor-format.md).

<a id="gdb-stub"></a>
**GDB stub** — A debug server inside QEMU (enabled by `-s`, i.e. `-gdb tcp::1234`) that a
host `gdb` connects to with `target remote :1234`. With `-S` the CPU halts at reset. See
[debugging with GDB](../guides/debugging-with-gdb.md).

## I

<a id="ivt"></a>
**IVT (Interrupt Vector Table)** — The real-mode table at physical `0x00000`: 256 entries
of 4 bytes (segment:offset) per interrupt. Used by [BIOS services](../concepts/bios-services.md);
irrelevant once in protected mode.

## L

<a id="libc"></a>
**libc** — The C standard library. Absent here; there is no `printf`, `malloc`, or
`strlen` unless the kernel writes its own. See [freestanding C](../concepts/freestanding-c.md).

<a id="linker-script"></a>
**Linker script** — A file (`linker.ld`) telling `ld` where to place each section. It puts
`.text` at `0x1000` so execution starts at the first byte. See
[linker scripts](../concepts/linker-scripts.md).

## M

<a id="mmio"></a>
**MMIO (Memory-Mapped I/O)** — Talking to a device by reading/writing ordinary memory
addresses instead of [I/O ports](io-ports.md). The VGA framebuffer at `0xB8000` is MMIO.

## N

<a id="nasm"></a>
**NASM** — The Netwide Assembler, used for the boot sector (`-f bin`) and the kernel entry
stub (`-f elf32`). See [toolchain-and-build](toolchain-and-build.md).

## P

<a id="paging"></a>
**Paging** — Hardware translation of virtual to physical addresses via page tables.
**Not used in MyOS-Simple** — every address is physical, which is why the
[memory map](memory-map.md) lists physical addresses throughout.

<a id="pcb"></a>
**PCB (Process Control Block)** — The struct holding a process's PID, name, state, and
saved context. The unit the [scheduler](../concepts/cooperative-scheduling.md) manages.

<a id="polling-vs-interrupts"></a>
**Polling vs interrupts** — Polling repeatedly reads a status register to check for events;
interrupts let the device signal the CPU. MyOS-Simple polls the
[keyboard](../concepts/ps2-keyboard-8042.md) (status bit 0 of port `0x64`).

<a id="post"></a>
**POST (Power-On Self-Test)** — The firmware's hardware check at startup, before it loads
the [boot sector](../concepts/boot-sector.md).

<a id="protected-mode"></a>
**Protected mode** — The 32-bit CPU mode with segment descriptors, privilege rings, and a
4 GiB address space, entered by setting [CR0.PE](#cr0-pe). See
[protected mode](../concepts/protected-mode.md).

## R

<a id="real-mode"></a>
**Real mode** — The 16-bit mode the CPU boots into: 1 MiB address space,
[segment:offset](#segment-offset) addressing, BIOS services available. See
[real mode](../concepts/real-mode.md).

<a id="round-robin"></a>
**Round-robin** — Scheduling that cycles through ready tasks in order, each getting a turn.
MyOS-Simple's scheduler is cooperative round-robin — see
[cooperative scheduling](../concepts/cooperative-scheduling.md).

## S

<a id="scancode"></a>
**Scancode (Set 1, make/break)** — The byte a key sends. A **make** code on press, a
**break** code (make `| 0x80`) on release. Translated via the
[scancode tables](scancode-tables.md). See [scancodes](../concepts/scancodes.md).

<a id="sector"></a>
**Sector** — The smallest addressable disk unit, 512 bytes here. The image is padded to a
whole number of sectors with `truncate` — see [toolchain-and-build](toolchain-and-build.md).

<a id="segment-descriptor"></a>
**Segment descriptor** — An 8-byte [GDT](#gdt) entry defining a segment's base, limit, and
access rights. Decoded bit-by-bit in the [descriptor format reference](gdt-descriptor-format.md).

<a id="segment-offset"></a>
**Segment:offset** — Real-mode addressing where a linear address is `segment × 16 + offset`.
Replaced by [selectors](#selector) in protected mode. See [real mode](../concepts/real-mode.md).

<a id="selector"></a>
**Selector** — A value loaded into a segment register that indexes the [GDT](#gdt) (low 3
bits = RPL and table indicator). `CODE_SEG = 0x08`, `DATA_SEG = 0x10`. See
[descriptor format](gdt-descriptor-format.md).

## U

<a id="uip-flag"></a>
**UIP flag (Update-In-Progress)** — Bit in CMOS Status Register A signaling the RTC is
mid-update; reading time fields during it can return inconsistent values. MyOS-Simple does
not synchronize against it — see [CMOS / RTC](../concepts/cmos-rtc.md).

## V

<a id="vga-text-mode"></a>
**VGA text mode** — An 80×25 grid of character cells at [`0xB8000`](memory-map.md), each a
[character byte](#framebuffer) plus an [attribute byte](#attribute-byte). See
[VGA text mode](../concepts/vga-text-mode.md).

## Q

<a id="qemu"></a>
**QEMU** — The machine emulator that boots the disk images (`qemu-system-x86_64 -drive
format=raw,file=…`) and provides the [GDB stub](#gdb-stub). See
[toolchain-and-build](toolchain-and-build.md).

## Other

<a id="8042-controller"></a>
**8042 controller** — The PS/2 keyboard controller chip. Status on port `0x64`, data on
`0x60`. See [PS/2 keyboard & the 8042](../concepts/ps2-keyboard-8042.md).

> 💡 **Tidbit:** `0xAA55` is stored on disk little-endian, so a hex dump of the boot sector
> shows the bytes `55 AA` at offsets 510 and 511 — not `AA 55`. Reading them as a 16-bit
> word gives `0xAA55` back.

> ⚠️ **Caveat:** "Protected mode" does not by itself mean memory *protection* between
> tasks. With a [flat model](#flat-memory-model) and no [paging](#paging), every task can
> read and write all 4 GiB; the term refers to the privilege/segment machinery, not
> isolation.

> 💡 **Tidbit:** `eflags` bit 9 (`0x200`) is the Interrupt Flag (IF). The bootloader's
> `cli` clears it before switching modes so a stray interrupt cannot fire while the
> [IVT](#ivt) is no longer valid and the [GDT](#gdt)/IDT are mid-setup.

## See also

- [Memory map](memory-map.md) · [I/O ports](io-ports.md) · [GDT descriptor format](gdt-descriptor-format.md)
- [Scancode tables](scancode-tables.md) · [Command reference](command-reference.md) · [Toolchain and build](toolchain-and-build.md)
- Concepts: [boot process](../concepts/boot-process.md), [real mode](../concepts/real-mode.md), [protected mode](../concepts/protected-mode.md)
- [Home](../Home.md)
