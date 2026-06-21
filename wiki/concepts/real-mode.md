[← Home](../Home.md)

# Real Mode

*The 16-bit world every x86 starts in — segments, offsets, and a 1 MiB ceiling that MyOS-Simple lives within until it switches to protected mode.*

Every x86 CPU, from the original 8086 to the latest 64-bit chip, powers up in
**real mode**. It is a compatibility museum baked into silicon: a 16-bit
execution environment that behaves almost exactly like a 1978 Intel 8086. When
the BIOS jumps to our [boot sector](boot-sector.md) at `0x7C00`, this is the
environment our code inherits.

## What "real mode" means

Real mode is defined by a handful of properties:

- **16-bit registers.** The general-purpose registers `AX`, `BX`, `CX`, `DX`,
  the index registers, and the stack pointer are all 16 bits wide. The largest
  value a single register holds is `0xFFFF` (65535).
- **Segment:offset addressing.** Memory is reached through a pair of 16-bit
  values, not a single flat pointer (see below).
- **~1 MiB of addressable memory.** The addressing formula tops out just past
  one megabyte.
- **No memory protection.** Any code can read or write any address. There are no
  privilege rings, no page faults, no isolation. A stray pointer corrupts
  whatever it hits.
- **BIOS services are available.** Because the [IVT](../reference/memory-map.md)
  is live, software interrupts like `int 0x10` and `int 0x13` work. These vanish
  the instant we enter [protected mode](protected-mode.md).

This is why all of MyOS-Simple's boot-time hardware work — clearing the screen,
[loading the kernel from disk](disk-loading-int13.md) — happens *in real mode,
before* the protected-mode switch.

## Segment:offset addressing

The defining feature of real mode is how it forms a physical address. The 8086
had 16-bit registers but a 20-bit address bus — it could *point* at 64 KiB but
*wire* up to 1 MiB. Intel bridged the gap with **segmentation**: every memory
access combines two 16-bit values.

```text
physical address = (segment << 4) + offset
                 = segment * 16   + offset
```

The segment register supplies a base, shifted left by 4 bits (multiplied by 16),
and the offset is added on top. So the logical address `0x07C0:0x0000` and the
logical address `0x0000:0x7C00` both resolve to the same physical byte:

```text
0x07C0 * 16 + 0x0000 = 0x7C00 + 0      = 0x7C00
0x0000 * 16 + 0x7C00 = 0      + 0x7C00 = 0x7C00
```

This is exactly why MyOS-Simple's bootloader zeroes its segment registers up
front:

```asm
    ; Set up segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
```

`helloworld-os-c/boot.asm:21-26`

With `DS = ES = SS = 0`, every offset *is* its own physical address — `mov bx,
0x1000` simply means physical `0x1000`, with no segment arithmetic to track in
your head. The `[ORG 0x7C00]` directive at the top of each boot file tells the
assembler to compute label addresses as if the code lives at offset `0x7C00`,
which (with a zero `DS`) matches the physical address the BIOS loaded us to.

> 💡 **Tidbit:** Because `segment * 16 + offset` can be written many ways, the
> same physical byte has up to 4096 different `segment:offset` spellings. Two
> addresses are *aliases* if they reduce to the same physical value. Real-mode
> programmers learned to normalise their pointers to avoid surprises.

## The 1 MiB ceiling (and a little extra)

The largest address real mode can name comes from maxing out both halves of the
formula:

```text
0xFFFF * 16 + 0xFFFF = 0xFFFF0 + 0xFFFF = 0x10FFEF  (~1 MiB + ~64 KiB)
```

On the original 8086, the address bus was only 20 bits wide, so anything past
`0xFFFFF` (1 MiB) silently **wrapped around** to the bottom of memory. The
practical ceiling was therefore ~1 MiB.

> 💡 **Tidbit:** Real mode's 20-bit reach is a direct consequence of the
> `segment * 16 + offset` formula: shifting a 16-bit segment left by 4 and
> adding a 16-bit offset produces at most a 21-bit sum, but the original bus
> truncated it to 20 bits — capping classic real mode at ~1 MiB.

> ⚠️ **Caveat:** Later CPUs (80286+) had wider address buses, so the wrap no
> longer happened automatically. The small region just above 1 MiB
> (`0x100000`–`0x10FFEF`) became reachable from real mode and is called the
> **High Memory Area (HMA)** — but only if the **A20 line** (address line 20) is
> enabled. MyOS-Simple does not need the HMA: it loads everything into low
> memory and switches to protected mode before it would matter.

## The memory map in real mode

Here is what low memory looks like while MyOS-Simple's boot sector is running.
The full table lives in the [memory map reference](../reference/memory-map.md);
the highlights:

| Physical address | Contents |
|------------------|----------|
| `0x00000` | Interrupt Vector Table (IVT) + BIOS data area |
| `0x00500` | First freely usable low memory |
| `0x01000` | Where the kernel is loaded (stage 2+) |
| `0x07C00` | The boot sector (512 bytes, ends in `0xAA55`) |
| `0x07E00` | Free memory above the boot sector |
| `0xB8000` | VGA text-mode framebuffer |

The IVT at `0x00000` is what makes [BIOS services](bios-services.md) work: each
`int N` instruction looks up entry `N` in that table and calls the firmware
routine it points to.

## The stack in real mode

MyOS-Simple sets `SP = 0x7C00` (`boot.asm:26`). The x86 stack grows *downward* —
each `push` decrements `SP` before writing. Starting the stack at `0x7C00` and
the code also at `0x7C00` is deliberate: the stack expands into the free memory
*below* the boot sector (`0x7BFF`, `0x7BFE`, …), so it can never collide with
the 512 bytes of code sitting at `0x7C00` and above.

> ⚠️ **Caveat:** Real mode gives you no stack-overflow protection. If the stack
> grows far enough down it will eventually run into the IVT/BIOS data at
> `0x00000` and corrupt it silently. Boot code keeps its stack usage tiny for
> exactly this reason.

## Why leave real mode at all?

Real mode is friendly for booting — the BIOS does the hardware legwork for you —
but it is a poor home for a real kernel:

- Only ~1 MiB of memory is reachable.
- 16-bit registers make 32-bit arithmetic clumsy.
- There is no memory protection, so a bug anywhere can corrupt anything.
- Segment juggling makes large, flat data structures painful.

So from [stage 2](../stages/stage-2-c-protected-mode.md) onward, the bootloader
installs a [Global Descriptor Table](global-descriptor-table.md), flips
`CR0.PE`, and far-jumps into [protected mode](protected-mode.md): flat 32-bit
addressing, 4 GiB of reach, and privilege levels. The
[boot process](boot-process.md) page walks that transition step by step.

Real mode never fully disappears, though — it is still the only environment in
which the convenient [BIOS services](bios-services.md) exist, which is why
single-sector [stage 1](../stages/stage-1-assembly-boot.md) stays in real mode
for its entire life.

## See also

- [The boot process](boot-process.md) — the full power-on-to-C walkthrough
- [BIOS services](bios-services.md) — the interrupts real mode makes available
- [The boot sector](boot-sector.md) — the 512-byte program real mode first runs
- [Protected mode](protected-mode.md) — the 32-bit mode MyOS switches to
- [The Global Descriptor Table](global-descriptor-table.md) — what enables the switch
- [Memory map](../reference/memory-map.md) — every fixed address in low memory
- [Glossary](../reference/glossary.md) — segment, offset, A20, IVT, and more
- [Home](../Home.md)
