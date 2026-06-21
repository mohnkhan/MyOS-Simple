[← Home](../Home.md)

# The Boot Sector

*Exactly 512 bytes, loaded to 0x7C00, ending in the magic word 0xAA55 — the smallest unit of code the firmware will ever run for you.*

The **boot sector** is the first sector of a bootable disk: precisely 512 bytes,
the last two of which must be the signature `0xAA55`. It is the entry point for
*everything* MyOS-Simple does — in [stage 1](../stages/stage-1-assembly-boot.md)
it is the entire operating system, and in [stage 2+](boot-process.md) it is a
small bootloader that pulls in a larger kernel. This page explains the strict
contract a boot sector must satisfy and the assembly idioms MyOS-Simple uses to
meet it.

## The contract

For the BIOS to load and run a sector, three conditions must hold:

1. **It is exactly 512 bytes.** No more, no less. The BIOS reads exactly one
   sector (512 bytes on standard media) to `0x7C00`.
2. **Its last two bytes are the boot signature** `0x55` then `0xAA` — read as a
   little-endian 16-bit word, `0xAA55`.
3. **It is valid 16-bit code** that begins executing at offset 0, because the
   BIOS far-jumps to the start of what it loaded, in
   [real mode](real-mode.md).

If any of these fail, the BIOS treats the device as non-bootable and moves to the
next one in its boot order. The [boot process](boot-process.md) page covers how
the firmware finds and loads this sector; here we focus on *building* one
correctly.

## The `0xAA55` signature

The signature is the firmware's single, blunt test for "is this disk bootable?".
It is stored in the **last two bytes** of the sector:

```text
byte 510: 0x55
byte 511: 0xAA
```

Because x86 is **little-endian**, those two bytes read back as the 16-bit value
`0xAA55` (low byte first). This is a constant source of confusion — the value
people quote (`0xAA55`) has its bytes in the *opposite* order from how they sit
on disk (`55 AA`).

> 💡 **Tidbit:** The `0xAA55` signature is the firmware's *only* sanity check
> that a disk is bootable. There is no checksum, no header, no filesystem
> inspection — just two magic bytes. Write `0xAA55` at offset 510 of any 512-byte
> sector and the BIOS will happily jump into it, valid code or not.

> ⚠️ **Caveat:** Get the byte order wrong — emitting `0x55AA` instead of
> `0xAA55` — and the disk is silently treated as non-bootable. The NASM idiom
> `dw 0xAA55` is correct because `dw` writes a 16-bit word in little-endian
> order, producing `55 AA` on disk, which the firmware reads back as `0xAA55`.

## Building a 512-byte sector with NASM

MyOS-Simple's boot files share the same three structural directives. Here is the
end of the monochrome stage 1 build:

```asm
[BITS 16]
[ORG 0x7C00]

start:
    ; ... code ...

times 510 - ($ - $$) db 0
dw 0xAA55
```

`helloworld-os-asm/main.asm:10-11, 68-69`

Three things are doing the heavy lifting:

### `[BITS 16]` — assemble 16-bit code

The boot sector runs in [real mode](real-mode.md), so the assembler must emit
16-bit instruction encodings. `[BITS 16]` tells NASM exactly that. (The
bootloader later switches to `[BITS 32]` *after* the protected-mode jump — see
`helloworld-os-c/boot.asm:47`.)

### `[ORG 0x7C00]` — origin at the load address

`[ORG 0x7C00]` tells NASM to compute every label's address as if the code were
loaded at `0x7C00`. This matters because the boot sector refers to its own data
by absolute address — for example `mov si, msg` needs `msg` to resolve to the
right *physical* location. Since the BIOS loads us to `0x7C00` and the bootloader
zeroes its segments (`DS = 0`), the assembler's offsets line up exactly with the
physical addresses at runtime. (Why `0x7C00`? The history is on the
[boot process](boot-process.md) page.)

### The padding idiom

```asm
times 510 - ($ - $$) db 0
dw 0xAA55
```

This is the canonical way to pad a boot sector to exactly 512 bytes with the
signature on the end. Reading it piece by piece:

- `$` is the current address (the byte right after the code).
- `$$` is the address of the start of the section.
- `$ - $$` is therefore the number of bytes of code and data written so far.
- `510 - ($ - $$)` is how many bytes are left before offset 510.
- `times N db 0` emits `N` zero bytes — filling the gap so the code occupies
  exactly bytes 0–509.
- `dw 0xAA55` writes the 2-byte signature into bytes 510–511.

Total: exactly 512 bytes, signature at the end. Both stage 1 builds and the
stage 2+ bootloader use this identical pattern
(`helloworld-os-asm/main_color.asm:192-193`,
`helloworld-os-c/boot.asm:140-141`).

> 💡 **Tidbit:** If your code plus data exceeds 510 bytes, `510 - ($ - $$)`
> becomes negative and NASM throws a `times value is negative` error at assembly
> time. That error message is really telling you "your boot sector doesn't fit in
> 512 bytes" — a hard size ceiling that is the whole reason
> [stage 2+](boot-process.md) splits into a bootloader plus a separately loaded
> kernel.

## One sector is rarely enough

512 bytes is tiny. After the signature you have only **510 bytes** for code and
data. Stage 1 squeezes a whole interactive program into that budget by writing
hand-tuned assembly and leaning on [BIOS services](bios-services.md) for all the
hard work. But a C kernel does not fit, so the boot sector evolves into a
two-part design:

| Stage | Boot sector role | What it does |
|-------|------------------|--------------|
| [Stage 1](../stages/stage-1-assembly-boot.md) | *Is* the OS | Prints, reads a key, halts — all in 512 B |
| [Stage 2+](../stages/stage-2-c-protected-mode.md) | Bootloader | Loads a multi-sector kernel, enters protected mode, calls C |

In the bootloader case, the boot sector lives at disk **sector 1**, and the
kernel occupies the contiguous sectors starting at **sector 2**. The boot sector
reads those extra sectors into memory with
[INT 13h disk loading](disk-loading-int13.md) before handing off. The number of
sectors read grows with the kernel across the stages (16 / 15 / 39 / 39).

## Where the boot sector sits in memory

| Physical address | Contents |
|------------------|----------|
| `0x07C00` | The boot sector — 512 bytes, ending in `0xAA55` |
| `0x07DFE` | byte 510 = `0x55` |
| `0x07DFF` | byte 511 = `0xAA` |
| `0x07E00` | First free byte above the boot sector |

The boot sector also sets its stack just below itself (`SP = 0x7C00`,
`helloworld-os-c/boot.asm:26`), so the downward-growing stack uses the free
memory below `0x7C00` without ever touching the loaded code. The complete layout
is in the [memory map](../reference/memory-map.md).

## See also

- [The boot process](boot-process.md) — how the firmware finds and runs this sector
- [Real mode](real-mode.md) — the 16-bit environment the sector executes in
- [BIOS services](bios-services.md) — the firmware routines a boot sector relies on
- [Disk loading with INT 13h](disk-loading-int13.md) — loading the kernel from sector 2 onward
- [Linker scripts](linker-scripts.md) — laying out the *kernel* binary the bootloader loads
- [Memory map](../reference/memory-map.md) — where `0x7C00` and `0xAA55` live
- [Toolchain and build](../reference/toolchain-and-build.md) — assembling and imaging a boot sector
- [Stage 1: assembly boot](../stages/stage-1-assembly-boot.md)
- [Home](../Home.md)
