# Protected Mode

*The 32-bit operating mode the kernel runs in — flat memory, ring 0, and no BIOS safety net.*

After the BIOS finishes and the bootloader takes over, the CPU is still running in
**real mode**: a 16-bit, 1 MiB-addressable compatibility mode that has been kept
alive on every x86 chip since the 8086. To run a modern C kernel with a flat
4 GiB address space, MyOS-Simple switches the processor into **protected mode**.
This page explains what protected mode is, why the project needs it, and walks
through the exact switch performed in [`boot.asm`](../stages/stage-2-c-protected-mode.md).

## Why switch at all?

[Real mode](real-mode.md) is convenient at boot — the [BIOS services](bios-services.md)
are available, and `segment:offset` addressing matches what the firmware expects —
but it is also extremely limiting:

- Addresses are formed as `segment * 16 + offset`, capping reach at ~1 MiB.
- Registers and the default operand size are 16-bit.
- There is no memory protection, no privilege separation, and no clean 32-bit model.

Protected mode lifts these limits. It gives the CPU:

- **32-bit registers and operands** by default, so a C compiler targeting `-m32`
  produces code that just works.
- **A 4 GiB linear address space** addressable with plain 32-bit pointers.
- **Segment descriptors** (held in the [Global Descriptor Table](global-descriptor-table.md))
  that define each segment's base, limit, and access rights, instead of the fixed
  `*16` arithmetic of real mode.
- **Privilege rings** (0–3), though this project only ever uses ring 0.

> 💡 **Tidbit:** "Protected" refers to *memory protection* — the descriptor system
> can restrict access by privilege level and segment limit. MyOS-Simple does not
> actually use those protections for isolation; it sets up a single flat ring-0
> model and runs everything there. The name is historical baggage from the 286.

## The flat memory model

MyOS-Simple uses the simplest possible protected-mode layout: the **flat model**.
Every segment (code and data) is configured with base `0` and limit `4 GiB`, so
each one spans the entire address space `0x00000000 .. 0xFFFFFFFF`. Because the
base is zero and the limit covers everything, the `segment:offset` translation
collapses: the effective address is simply the offset, i.e. a plain 32-bit
**linear address**.

This project never enables **paging**, so there is no page-table translation
between linear and physical addresses. The result is the simplest mental model
you can have:

```text
logical (segment:offset)  ->  linear address  ->  physical address
        offset            ==      offset       ==      offset
```

> 💡 **Tidbit:** Because paging is off, **linear address == physical address**
> throughout the entire kernel. When the C code writes to `0xB8000`
> (see [`kernel.c:52`](vga-text-mode.md)), it is touching that exact physical
> byte of [VGA text memory](vga-text-mode.md) — no translation layer in between.

The two segment descriptors that establish this model are defined in the
[GDT](global-descriptor-table.md); the exhaustive bit-by-bit breakdown lives in
[`reference/gdt-descriptor-format.md`](../reference/gdt-descriptor-format.md).

## The switch, step by step

The transition happens in [`boot.asm:39-63`](../stages/stage-2-c-protected-mode.md).
Here is the real code from the bootloader:

```asm
    ; Switch to protected mode
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEG:init_pm

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

### 1. `cli` — disable interrupts

```asm
cli
```

Real-mode interrupt handlers live in the BIOS-provided Interrupt Vector Table at
the bottom of memory. Once we change the CPU's operating mode, those handlers are
no longer valid, and this project never installs an IDT to replace them. So we
clear the interrupt flag and simply leave hardware interrupts off for the entire
life of the kernel. Input is handled by **polling** instead — see how
[`shell.c:145`](ps2-keyboard-8042.md) busy-waits on the keyboard status port.

> ⚠️ **Caveat:** Because interrupts are never re-enabled, the kernel cannot be
> driven by a timer interrupt. The [cooperative scheduler](cooperative-scheduling.md)
> in stage 4 works around this by having tasks voluntarily yield, rather than
> being preempted by a clock tick.

### 2. `lgdt [gdt_descriptor]` — load the GDT

```asm
lgdt [gdt_descriptor]
```

`lgdt` loads the **GDT register (GDTR)** from a 6-byte structure describing where
the table is and how big it is. The CPU needs valid segment descriptors *before*
we enter protected mode, because the very next selector load must resolve against
the GDT. The structure is:

```asm
gdt_descriptor:
    dw gdt_end - gdt_start - 1   ; limit (size in bytes, minus one)
    dd gdt_start                 ; base (linear address of the table)
```

The full table is covered in [`global-descriptor-table.md`](global-descriptor-table.md).

### 3. Set `CR0.PE` — flip the mode bit

```asm
mov eax, cr0
or eax, 0x1
mov cr0, eax
```

**Bit 0** of control register `CR0` is the **Protection Enable (PE)** flag.
`or eax, 0x1` sets exactly that bit, then writes it back. The instant this write
completes, the CPU is *technically* in protected mode — segment registers now
hold **selectors** that index the GDT, rather than the shifted base values of real
mode.

But there is a subtlety: the **`CS` register still holds its real-mode value**,
and the CPU is still executing the instruction stream that follows. The processor
keeps interpreting code with the old code-segment until `CS` is reloaded.

### 4. The mandatory far jump

```asm
jmp CODE_SEG:init_pm
```

This is the linchpin of the whole switch, and the one step beginners most often
get wrong. A **far jump** specifies both a segment selector and an offset:
`CODE_SEG` here is the value `0x08` (the byte offset of the code descriptor inside
the GDT). The far jump does two essential things at once:

1. **Reloads `CS`** with the protected-mode code selector, so the CPU finally
   starts decoding instructions using the 4 GiB flat code segment.
2. **Flushes the instruction prefetch/pipeline**, discarding any instructions the
   CPU fetched and decoded under the *old* 16-bit assumptions.

You cannot simply fall through into the 32-bit code — `CS` would never be reloaded
and the pipeline could contain stale, mis-decoded bytes. The far jump is what
makes the mode change actually take effect.

> 💡 **Tidbit:** Notice the `[BITS 32]` directive right at `init_pm`. This is an
> *assembler* directive telling NASM to emit 32-bit encodings from that point on;
> it does not change the CPU at runtime. The code *before* the far jump is still
> assembled `[BITS 16]`. Getting these directives to line up with the actual mode
> the CPU will be in is essential — a 32-bit encoding executed in 16-bit mode (or
> vice versa) decodes into garbage.

### 5. Reload the data segments and set up the stack

Now executing genuine 32-bit code, the bootloader loads every data segment
register (`DS`, `SS`, `ES`, `FS`, `GS`) with `DATA_SEG` (`0x10`, the data
descriptor's offset in the GDT). It then points the stack at `0x90000`:

```asm
    mov ebp, 0x90000
    mov esp, ebp
```

This puts a comfortable stack well above both the bootloader (`0x7C00`) and the
kernel (`0x1000`), with plenty of room to grow downward. See the
[memory map](../reference/memory-map.md) for the full picture of where everything
lives.

### 6. Hand off to the kernel

```asm
call KERNEL_OFFSET      ; KERNEL_OFFSET equ 0x1000
```

Finally the bootloader `call`s `0x1000`, the address where the kernel was loaded
from disk. The first byte there is `_start` (from
[`kernel_entry.asm`](freestanding-c.md)), which calls into C. The whole reason the
kernel is built as a [flat binary linked at 0x1000](linker-scripts.md) is so that
this single `call` lands exactly on the entry stub.

## The A20 line

There is one historical gotcha that this project quietly relies on the emulator to
handle. On the original PC, address line **A20** was forced low for 8086
compatibility, causing addresses at and above 1 MiB to *wrap around* to low
memory. To address all 4 GiB in protected mode you normally have to explicitly
enable the A20 line (commonly via the keyboard controller or fast-A20 port `0x92`).

MyOS-Simple **never enables A20 explicitly**.

> ⚠️ **Caveat:** This works under QEMU because the emulator enables A20 by default.
> On some real hardware, with A20 left disabled, any address with bit 20 set would
> wrap at the 1 MiB boundary, corrupting memory accesses above that line. A
> production bootloader would enable A20 before relying on high memory. For a
> tutorial that boots in an emulator, it is omitted for simplicity.

## See also

- [Global Descriptor Table](global-descriptor-table.md) — the segment table this mode requires
- [Real mode](real-mode.md) — the 16-bit mode we switch *out of*
- [Freestanding C](freestanding-c.md) — what the kernel can and cannot assume once we arrive here
- [Boot process](boot-process.md) — the full firmware-to-kernel journey
- [Stage 2: C in protected mode](../stages/stage-2-c-protected-mode.md) — the stage that introduces this switch
- [`reference/gdt-descriptor-format.md`](../reference/gdt-descriptor-format.md) — exhaustive descriptor bit tables
- [Memory map](../reference/memory-map.md) — where the GDT, stack, and kernel sit
- [Home](../Home.md)
