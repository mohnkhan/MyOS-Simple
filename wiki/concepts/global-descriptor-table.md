# The Global Descriptor Table (GDT)

*Three eight-byte entries that tell the CPU how to interpret every memory access in protected mode.*

To enter [protected mode](protected-mode.md), the CPU needs a table that defines
the segments it will use: where each one starts, how large it is, and what may be
done with it. That table is the **Global Descriptor Table (GDT)**. MyOS-Simple
installs a tiny, hand-written GDT in [`boot.asm`](../stages/stage-2-c-protected-mode.md)
with just three entries — the absolute minimum for a flat 32-bit kernel.

This page explains the *concept* of the GDT and walks through the project's table.
For the exhaustive, bit-by-bit field reference (every flag in the access and
granularity bytes), see
[`reference/gdt-descriptor-format.md`](../reference/gdt-descriptor-format.md).

## What a descriptor is

In real mode, a segment register directly holds a value that is multiplied by 16
to form a base address. In protected mode, a segment register instead holds a
**selector** — essentially an *index* into the GDT. The GDT entry it points to,
called a **segment descriptor**, is an 8-byte structure that encodes:

- **Base** — the linear start address of the segment (split awkwardly across the
  descriptor for backward-compatibility reasons).
- **Limit** — the size of the segment, scaled by the granularity flag.
- **Access byte** — present bit, privilege level, executable/data type, and
  read/write permissions.
- **Flags** — granularity, default operand size (16- vs 32-bit), and the 64-bit
  long-mode flag.

The CPU consults the descriptor on every memory access to validate and translate
the address. In the flat model, this validation is essentially a no-op, but the
descriptors still have to exist and be correct.

## The project's three-entry table

Here is the complete GDT from [`boot.asm:107-134`](../stages/stage-2-c-protected-mode.md):

```asm
; GDT
gdt_start:
    dd 0x0
    dd 0x0

gdt_code:
    dw 0xffff       ; Limit
    dw 0x0          ; Base
    db 0x0          ; Base
    db 10011010b    ; Access
    db 11001111b    ; Flags
    db 0x0          ; Base

gdt_data:
    dw 0xffff
    dw 0x0
    db 0x0
    db 10010010b
    db 11001111b
    db 0x0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start
```

### Entry 0: the null descriptor

```asm
gdt_start:
    dd 0x0
    dd 0x0
```

The first GDT entry **must** be all zeros. The x86 architecture reserves selector
`0x00` (the "null selector") as a deliberate trap: loading it into a data segment
register and then dereferencing it raises a fault. This catches the classic bug of
using an uninitialized segment register. The null descriptor occupies the slot but
is never used to address anything.

### Entry 1: the code segment

```asm
gdt_code:
    dw 0xffff       ; Limit  (bits 0..15)
    dw 0x0          ; Base   (bits 0..15)
    db 0x0          ; Base   (bits 16..23)
    db 10011010b    ; Access = 0x9A
    db 11001111b    ; Flags+limit-high = 0xCF
    db 0x0          ; Base   (bits 24..31)
```

This describes the segment from which the CPU **fetches instructions**. Base is
`0`, the limit field is `0xFFFFF`, and with 4 KiB granularity (see below) it spans
the full 4 GiB. The access byte `10011010b` (`0x9A`) marks it **present, ring 0,
a code segment, executable, non-conforming, and readable**.

### Entry 2: the data segment

```asm
gdt_data:
    dw 0xffff
    dw 0x0
    db 0x0
    db 10010010b    ; Access = 0x92
    db 11001111b    ; Flags+limit-high = 0xCF
    db 0x0
```

Byte-for-byte identical to the code segment **except the access byte**, which is
`10010010b` (`0x92`): **present, ring 0, a data segment, grows-up, writable**.
This is the segment used for `DS`, `SS`, `ES`, `FS`, and `GS`. It overlaps the
code segment completely — both cover all 4 GiB — which is exactly what "flat
model" means.

> 💡 **Tidbit:** The code and data segments differ by a single bit (the
> *executable* bit, E). `0x9A` vs `0x92` is the difference between "the CPU may
> jump here and run it" and "the CPU may read and write here." Everything else
> about the two segments is identical, which is why they can occupy the same
> address range.

## Decoding the two interesting bytes

You do not need to memorize these — [`reference/gdt-descriptor-format.md`](../reference/gdt-descriptor-format.md)
has the full tables — but seeing them once builds intuition.

### The access byte

Bits, from 7 down to 0, are `P | DPL[1] DPL[0] | S | E | DC | RW | A`:

| Value | Bits | Meaning |
|-------|------|---------|
| `0x9A` (code) | `1 00 1 1 0 1 0` | Present, ring 0, code/data type, executable, non-conforming, readable, not accessed |
| `0x92` (data) | `1 00 1 0 0 1 0` | Present, ring 0, code/data type, **not** executable (data), grows-up, writable, not accessed |

`DPL = 00` is what makes this a **ring 0** kernel — the most privileged level.

### The flags / limit-high byte: `0xCF` (`11001111b`)

The high nibble is the **flags**, the low nibble is **limit bits 19:16**:

| Nibble | Value | Meaning |
|--------|-------|---------|
| Flags (high) | `1100` = `0xC` | G=1 (4 KiB granularity), D/B=1 (32-bit), L=0 (not 64-bit), AVL=0 |
| Limit[19:16] (low) | `1111` = `0xF` | top 4 bits of the 20-bit limit |

> 💡 **Tidbit:** How do you get a 4 GiB segment from a 20-bit limit field? The
> **granularity bit (G)** scales the limit by 4 KiB. With the full limit field
> `0xFFFFF` and G=1, the segment size is `(0xFFFFF + 1) * 4 KiB = 0x100000 * 0x1000
> = 4 GiB`. Without granularity, the same field would top out at just 1 MiB.

## How the descriptor is registered: `gdt_descriptor`

```asm
gdt_descriptor:
    dw gdt_end - gdt_start - 1   ; limit = (size of table) - 1
    dd gdt_start                 ; base  = linear address of the table
```

The `lgdt` instruction loads the **GDT register (GDTR)** from this 6-byte
structure — a 16-bit limit followed by a 32-bit base. The limit is `gdt_end -
gdt_start - 1`: for three 8-byte entries that is `24 - 1 = 23`.

> 💡 **Tidbit:** The limit is **size minus one** by design. A limit value of `0`
> has to mean "1 byte is valid," not "0 bytes," so the field always encodes the
> *largest valid offset* rather than the count. This same minus-one convention
> appears in the segment descriptors' own limit fields.

## Selectors: turning a label into a segment register value

```asm
CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start
```

A **selector** is the value you actually load into a segment register, and it is
simply the **byte offset** of the descriptor within the GDT:

- `CODE_SEG = gdt_code - gdt_start = 8` → `0x08`
- `DATA_SEG = gdt_data - gdt_start = 16` → `0x10`

This is why the [protected-mode switch](protected-mode.md) far-jumps to
`CODE_SEG:init_pm` (loading `CS` with `0x08`) and then loads `0x10` into every
data segment register. The low 3 bits of a real selector also encode the requested
privilege level and a table-indicator bit; because this project only uses ring 0
and only the GDT, those bits are all zero, so the offsets `0x08`/`0x10` are the
selectors verbatim.

> ⚠️ **Caveat:** This GDT is *static* — it is assembled into the boot sector and
> never modified at runtime. A fuller OS would later add a **Task State Segment
> (TSS)** descriptor (needed for ring transitions and hardware task switching) and
> separate user-mode (ring 3) code/data descriptors. MyOS-Simple needs none of
> that because it runs entirely in ring 0 with a single flat address space.

## See also

- [`reference/gdt-descriptor-format.md`](../reference/gdt-descriptor-format.md) — the complete bit-field reference for every descriptor field
- [Protected mode](protected-mode.md) — why and how the GDT is loaded
- [Real mode](real-mode.md) — the `segment * 16` model the GDT replaces
- [Memory map](../reference/memory-map.md) — where the GDT lives in memory
- [Stage 2: C in protected mode](../stages/stage-2-c-protected-mode.md) — the stage that introduces the GDT
- [Home](../Home.md)
