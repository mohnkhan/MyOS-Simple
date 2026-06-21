[← Home](../Home.md)

# GDT Descriptor Format

*The exhaustive, bit-by-bit reference for the 8-byte segment descriptor — every field in MyOS-Simple's three GDT entries decoded.*

This is the field-level companion to the [Global Descriptor Table](../concepts/global-descriptor-table.md)
concept page. Where that page explains *why* the table exists, this page is the
authoritative decode of *every bit* in the descriptors installed by
[`boot.asm`](../stages/stage-2-c-protected-mode.md) (`gdt_start` at `boot.asm:107`).

## The 8-byte descriptor

A segment descriptor is 8 bytes. The base and limit fields are split across
non-contiguous byte ranges — a historical artifact of the 286 → 386 evolution.

| Byte | Bits held | Field |
|------|-----------|-------|
| 0 | limit[15:0] low byte | Segment limit, bits 0–7 |
| 1 | limit[15:0] high byte | Segment limit, bits 8–15 |
| 2 | base[15:0] low byte | Base address, bits 0–7 |
| 3 | base[15:0] high byte | Base address, bits 8–15 |
| 4 | base[23:16] | Base address, bits 16–23 |
| 5 | access byte | Present, DPL, type — see below |
| 6 | flags (high nibble) ⎢ limit[19:16] (low nibble) | Granularity flags + top of limit |
| 7 | base[31:24] | Base address, bits 24–31 |

So the limit is **20 bits** (16 + 4) and the base is **32 bits** (16 + 8 + 8).

## MyOS-Simple's three entries

From `boot.asm:107-127`:

| Entry | Selector | limit | base | access | flags+limit-hi | Raw bytes (LE) |
|-------|----------|-------|------|--------|----------------|----------------|
| NULL | `0x00` | — | — | — | — | `00 00 00 00 00 00 00 00` |
| CODE | `0x08` | `0xFFFF` | `0x000000` | `0x9A` (`10011010b`) | `0xCF` (`11001111b`) | `FF FF 00 00 00 9A CF 00` |
| DATA | `0x10` | `0xFFFF` | `0x000000` | `0x92` (`10010010b`) | `0xCF` (`11001111b`) | `FF FF 00 00 00 92 CF 00` |

```asm
gdt_start:
    dd 0x0
    dd 0x0          ; NULL descriptor

gdt_code:
    dw 0xffff       ; limit[15:0]
    dw 0x0          ; base[15:0]
    db 0x0          ; base[23:16]
    db 10011010b    ; access  = 0x9A
    db 11001111b    ; flags|limit[19:16] = 0xCF
    db 0x0          ; base[31:24]

gdt_data:
    dw 0xffff
    dw 0x0
    db 0x0
    db 10010010b    ; access  = 0x92
    db 11001111b
    db 0x0
```

## The access byte (byte 5)

Bits, high to low:

| Bit | Name | Meaning |
|-----|------|---------|
| 7 | P | Present — segment is valid |
| 6–5 | DPL | Descriptor Privilege Level (ring 0–3) |
| 4 | S | Descriptor type: 1 = code/data, 0 = system |
| 3 | E | Executable: 1 = code segment, 0 = data segment |
| 2 | DC | Code: conforming. Data: direction (0 = grows up) |
| 1 | RW | Code: readable. Data: writable |
| 0 | A | Accessed (set by CPU on use) |

### Decode CODE access `0x9A` = `1 00 1 1 0 1 0`

| Bit(s) | Value | Interpretation |
|--------|-------|----------------|
| P | 1 | Present |
| DPL | 00 | Ring 0 |
| S | 1 | Code/data segment |
| E | 1 | Executable → **code** |
| DC | 0 | Non-conforming |
| RW | 1 | Readable |
| A | 0 | Not yet accessed |

### Decode DATA access `0x92` = `1 00 1 0 0 1 0`

| Bit(s) | Value | Interpretation |
|--------|-------|----------------|
| P | 1 | Present |
| DPL | 00 | Ring 0 |
| S | 1 | Code/data segment |
| E | 0 | Not executable → **data** |
| DC | 0 | Direction up (expand-up) |
| RW | 1 | Writable |
| A | 0 | Not yet accessed |

## The flags nibble (high nibble of byte 6)

| Bit | Name | Meaning |
|-----|------|---------|
| 7 | G | Granularity: 1 = limit counts 4 KiB pages, 0 = bytes |
| 6 | D/B | Default operand size: 1 = 32-bit |
| 5 | L | 64-bit code segment: 1 = long mode (must be 0 here) |
| 4 | AVL | Available for software use |

### Decode flags `0xC` = `1100`

| Bit | Value | Interpretation |
|-----|-------|----------------|
| G | 1 | 4 KiB granularity |
| D/B | 1 | 32-bit segment |
| L | 0 | Not 64-bit |
| AVL | 0 | Unused |

The **low nibble** of byte 6 is `limit[19:16] = 0xF`.

## How the limit becomes 4 GiB

The two limit pieces combine to `0xFFFFF` (20 bits). With granularity G = 1, the
limit is measured in 4 KiB units:

```text
span = (0xFFFFF + 1) x 4096
     = 0x100000 x 0x1000
     = 0x100000000
     = 4 GiB
```

So both code and data segments cover the entire 32-bit address space starting at base
`0x00000000` — a [flat memory model](glossary.md#flat-memory-model). Note the **+1**:
the limit field is *size minus one*.

## The GDTR operand (`gdt_descriptor`)

`lgdt` loads a 6-byte pseudo-descriptor (`boot.asm:129-131`):

```asm
gdt_descriptor:
    dw gdt_end - gdt_start - 1   ; 2-byte LIMIT = table size minus one
    dd gdt_start                 ; 4-byte BASE  = linear address of the table
```

| Field | Size | Value |
|-------|------|-------|
| Limit | 2 bytes | total table size − 1 (here 24 − 1 = 23) |
| Base | 4 bytes | linear address of `gdt_start` |

## Selectors

A selector is a byte offset into the GDT, not an index. Its low 3 bits hold the
Requested Privilege Level (bits 0–1) and the Table Indicator (bit 2). MyOS-Simple uses
RPL 0 and the GDT (TI = 0), so the selectors equal the raw offsets:

| Name | Value | Points to | RPL / TI |
|------|-------|-----------|----------|
| `CODE_SEG` | `0x08` | CODE descriptor (entry 1) | 0 / GDT |
| `DATA_SEG` | `0x10` | DATA descriptor (entry 2) | 0 / GDT |

`CODE_SEG equ gdt_code - gdt_start` and `DATA_SEG equ gdt_data - gdt_start`
(`boot.asm:133-134`). The far jump `jmp CODE_SEG:init_pm` (`boot.asm:45`) loads `CS`
with `0x08`; the data selectors are loaded into `DS/ES/FS/GS/SS` afterward.

> 💡 **Tidbit:** The descriptor layout looks bizarre because it grew. The 80286 had a
> 24-bit base and 16-bit limit packed into the first 6 bytes; the 80386 needed 32 bits
> of base and 20 of limit, so the extra bits were bolted onto bytes 6 and 7 to stay
> backward-compatible with existing 286 descriptors.

> ⚠️ **Caveat:** Entry 0 *must* be the null descriptor. Loading a segment register with
> selector `0x00` is legal but using it for a memory access raises a #GP fault. The CPU
> reserves index 0 deliberately, which is why the first usable selector is `0x08`.

> 💡 **Tidbit:** The GDTR limit and every segment limit are stored as *size minus one*.
> A 4 GiB segment cannot store "4 GiB" in a field whose maximum is `0xFFFFFFFF` — but
> "4 GiB − 1" fits exactly, and the hardware adds the 1 back.

## See also

- [Global Descriptor Table](../concepts/global-descriptor-table.md) — the concept and the table walkthrough
- [Protected mode](../concepts/protected-mode.md) — where the GDT is used
- [Stage 2: C in protected mode](../stages/stage-2-c-protected-mode.md) — `boot.asm` in context
- [Memory map](memory-map.md) — the flat 4 GiB space these descriptors span
- [Glossary](glossary.md) — selector, far jump, CR0/PE, A20
- [Home](../Home.md)
