# Memory Map

*Every physical address MyOS-Simple touches, from the interrupt vector table to the top of video memory.*

MyOS-Simple never enables paging, so every address in this reference is a **physical
address** — what the CPU puts on the bus is exactly what the RAM chip (or the VGA
card) sees. There is no virtual memory, no page tables, and no MMU translation. The
[flat memory model](../concepts/protected-mode.md) means a pointer in C and a
physical address are the same number.

This page is the authoritative layout for all C stages (2–5). The plan is identical
across them; only the kernel's size (and therefore the sector count loaded by the
bootloader) changes.

## The map at a glance

```text
 physical address
 0x00000  +-------------------------------+
          | Real-mode IVT + BIOS data     |   256 vectors x 4 bytes, then BDA
 0x00500  +-------------------------------+
          | (free low memory)             |
 0x01000  +-------------------------------+  <- KERNEL LOAD ADDRESS
          | Kernel image                  |     linker places .text here;
          |   .text .rodata .data .bss    |     execution starts at the first byte
          |   ...                         |
 0x07C00  +-------------------------------+  <- BOOT SECTOR (loaded by BIOS)
          | Boot sector (512 B, 0xAA55)   |     512 bytes, runs in real mode
 0x07E00  +-------------------------------+
          | (free)                        |
 0x90000  +-------------------------------+  <- PROTECTED-MODE STACK TOP
          | Stack (grows DOWNWARD)        |     ESP = EBP = 0x90000
          |              v v v            |
          +-------------------------------+
 0xB8000  +-------------------------------+  <- VGA TEXT FRAMEBUFFER (MMIO)
          | Video memory: 80x25 cells     |     each cell = char byte + attr byte
 0xC0000  +-------------------------------+  <- end of region of interest
```

## Region table

| Start | End | Region | Contents | Set up by | Notes |
|-------|-----|--------|----------|-----------|-------|
| `0x00000` | `0x004FF` | IVT + BIOS data area | 256 real-mode interrupt vectors (4 bytes each) followed by the BIOS Data Area | Firmware | Unused once in protected mode; the IVT no longer drives interrupts |
| `0x00500` | `0x00FFF` | Free low memory | Unused conventional RAM | — | Available scratch space |
| `0x01000` | ~`0x07BFF` | Kernel image | `.text`, `.rodata`, `.data`, `.bss` | [`linker.ld`](../concepts/linker-scripts.md) + [`boot.asm`](../stages/stage-2-c-protected-mode.md) disk load | `KERNEL_OFFSET equ 0x1000` in `boot.asm:15`; execution begins at the first byte |
| `0x07C00` | `0x07DFF` | Boot sector | The 512-byte [boot sector](../concepts/boot-sector.md), ending in `0xAA55` | BIOS (loads it here) | `[ORG 0x7C00]` in `boot.asm:13`; runs in [real mode](../concepts/real-mode.md) |
| `0x07E00` | `0x8FFFF` | Free | Unused | — | The stack lives at the top of this span |
| `0x90000` | (downward) | Stack | Protected-mode call stack | `boot.asm:57-58` | `ESP = EBP = 0x90000`, grows toward lower addresses |
| `0xB8000` | `0xBFFFF` | VGA text framebuffer | 80×25 character cells, [MMIO](glossary.md#mmio) | Hardware | Each cell is a char byte + an [attribute byte](../concepts/vga-text-mode.md) |
| `0xC0000` | — | End of interest | (Video BIOS / option ROMs beyond) | — | Outside what MyOS-Simple uses |

## Kernel sections (from `0x1000` upward)

The [linker script](../concepts/linker-scripts.md) lays the freestanding kernel out
in this order, starting at the load address:

| Order | Section | Holds | Initialized? |
|-------|---------|-------|--------------|
| 1 | `.text` | Executable code | Yes (in image) |
| 2 | `.rodata` | String literals, constant tables (e.g. the [scancode tables](scancode-tables.md)) | Yes (in image) |
| 3 | `.data` | Initialized globals | Yes (in image) |
| 4 | `.bss` | Zero-initialized globals | No (not in image) |

Because `.text` is placed first at `0x1000`, the bootloader can simply
`call 0x1000` (`boot.asm:61`) and land on the kernel's entry stub.

## Why these addresses

| Address | Rationale |
|---------|-----------|
| `0x7C00` | Firmware convention since the original IBM PC. It equals `32768 − 1024`: the last kilobyte of the first 32 KiB of RAM, leaving the low space free for the OS. |
| `0x1000` | Comfortably above the IVT/BDA and free low memory, and well below the boot sector at `0x7C00`, so the loaded kernel never overlaps the still-running bootloader. |
| `0x90000` | High enough to give the stack room to grow downward without colliding with the kernel, yet below the `0xA0000` legacy video/BIOS region. |
| `0xB8000` | Fixed hardware address of the VGA text-mode framebuffer; it is not RAM you chose but memory-mapped device memory. |

> 💡 **Tidbit:** `0x7C00` is `32768 − 1024`. The original IBM PC shipped with as
> little as 32 KiB of RAM, and the designers loaded the boot sector into the *last*
> kilobyte of that range so the freshly booted code had the maximum contiguous block
> of low memory beneath it.

> ⚠️ **Caveat:** The stack and the kernel image are not separated by any guard.
> `ESP` starts at `0x90000` and grows down; the kernel image grows up from `0x1000`.
> A runaway recursion or a huge local buffer can silently walk the stack down into
> free memory — there is no page fault to catch it because paging is off.

> 💡 **Tidbit:** `truncate -s 10240` pads stage 2/3 images to exactly 20 sectors
> (20 × 512 = 10240); stages 4 and 5 use `truncate -s 20480` (40 sectors). The pad
> only guarantees the file is large enough for the bootloader's CHS read — it does
> not affect the in-memory map above. See [toolchain-and-build.md](toolchain-and-build.md).

## See also

- [Boot process](../concepts/boot-process.md) — how control reaches each region
- [Boot sector](../concepts/boot-sector.md) — the `0x7C00` payload
- [Linker scripts](../concepts/linker-scripts.md) — how `.text` is placed at `0x1000`
- [Protected mode](../concepts/protected-mode.md) — the flat model that makes these physical addresses
- [VGA text mode](../concepts/vga-text-mode.md) — the `0xB8000` framebuffer
- [I/O ports](io-ports.md) — the device side of the map
- [Glossary](glossary.md)
- [Home](../Home.md)
