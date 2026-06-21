[← Home](../Home.md)

# Linker Scripts

*Telling the linker exactly where every byte goes — and why the kernel is a raw binary, not an ELF file.*

A compiler turns `kernel.c` into an object file (`kernel.o`) full of code and data,
but it does not decide *where in memory* that code will live, or *what file format*
the final image takes. That is the linker's job, and on a normal system the linker
uses sensible defaults. A bare-metal kernel cannot accept those defaults: it has to
land at one precise address with no extra wrapping. MyOS-Simple controls this with a
**linker script**, [`linker.ld`](../guides/building-and-running.md), driving the GNU
linker `ld`.

This page explains what a linker script is, walks through the project's script line
by line, and shows why a **flat binary** is mandatory here.

## What a linker does, and why we override it

When you link several object files, `ld` must:

1. Pick a **base address** where the program will be loaded.
2. **Merge** matching sections (all `.text`, all `.data`, …) from every input.
3. **Order** those sections in the output.
4. Choose an **output format** (ELF, PE, raw binary, …).
5. Resolve the **entry point** symbol.

On a hosted system the defaults handle all of this: programs are ELF, loaded by the
OS's program loader at a conventional address. A kernel has *no* OS loader beneath
it — the [bootloader](protected-mode.md) simply reads raw sectors from disk into
memory and jumps to them. So every one of those decisions has to be made
explicitly. The linker script is how.

## The complete script

Here is [`linker.ld`](../guides/building-and-running.md) in full:

```ld
ENTRY(_start)
OUTPUT_FORMAT(binary)

SECTIONS
{
    . = 0x1000;

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }
}
```

It is small, but every line is load-bearing.

### `ENTRY(_start)` — the entry symbol

```ld
ENTRY(_start)
```

This names `_start` as the program's entry point. `_start` is defined in
[`kernel_entry.asm`](freestanding-c.md), and all it does is `call kernel_main`.
With `OUTPUT_FORMAT(binary)` the entry symbol is not even recorded in the output
file (a flat binary has no header to store it in), but it still matters during the
link: it marks `_start` as a root so the linker keeps it and any code it reaches.

### `OUTPUT_FORMAT(binary)` — a raw flat binary

```ld
OUTPUT_FORMAT(binary)
```

This is the single most important line. It tells `ld` to emit a **flat binary**:
just the raw bytes of the sections, laid out back-to-back, with **no ELF header,
no symbol table, no relocation information, no metadata** of any kind. The output
is literally "the bytes that should appear in memory."

Why is this required? Because of how the kernel gets loaded. The
[bootloader](protected-mode.md) reads the kernel off the disk into memory at
`0x1000` and then executes:

```asm
call KERNEL_OFFSET      ; KERNEL_OFFSET equ 0x1000
```

There is **no ELF loader** in the boot path. Nothing parses program headers,
nothing applies relocations, nothing maps segments. The bootloader expects the
*first byte it loaded* to be the *first instruction to run*. An ELF file would
begin with a `0x7F 'E' 'L' 'F'` magic header — the CPU would try to execute that
header as code and immediately crash. A flat binary has no such wrapping.

> 💡 **Tidbit:** This is also why **debugging** is fiddly. `OUTPUT_FORMAT(binary)`
> discards all symbols and debug info, so a debugger attached to the running kernel
> sees only raw addresses. The usual workaround is to link a *second* time as ELF
> (or keep the `.o` files) purely to hand the symbol table to GDB. See
> [`guides/debugging-with-gdb.md`](../guides/debugging-with-gdb.md).

### `. = 0x1000;` — the location counter

```ld
SECTIONS
{
    . = 0x1000;
    ...
}
```

Inside a `SECTIONS` block, `.` is the **location counter** — the current output
address. Setting it to `0x1000` tells the linker "the first section starts at
address `0x1000`." This *must* match the address the bootloader loads the kernel
to. If the kernel were linked for, say, `0x0` but loaded at `0x1000`, every
absolute address baked into the code (function pointers, string literals, the
`0xB8000` writes are fine since they're constants, but jumps and calls are not)
would be off by `0x1000` and the kernel would jump into nonsense.

This fixed-address assumption is exactly why the kernel is compiled with
[`-fno-pic -fno-pie`](freestanding-c.md): position-independent code would be
unnecessary overhead when the load address is known and constant.

### Section ordering: `.text`, `.rodata`, `.data`, `.bss`

```ld
    .text   : { *(.text)   }   /* executable code            */
    .rodata : { *(.rodata) }   /* read-only data, string lits */
    .data   : { *(.data)   }   /* initialized read/write data */
    .bss    : { *(.bss)    }   /* zero-initialized data       */
```

Each output section gathers the matching input sections from every object file:
`*(.text)` means "the `.text` section of *all* inputs." Putting `.text` first is
deliberate — see the next section. The conventional order otherwise groups code,
then constants, then writable data, then the uninitialized `.bss`.

> 💡 **Tidbit:** `.bss` holds variables that start out zero. In an ELF image,
> `.bss` takes up *no* file space — the loader is expected to zero that region at
> load time. With a flat binary and no loader, there is nobody to do that zeroing.
> In this project it happens to be harmless because the kernel's globals are either
> explicitly initialized or written before they're read, and the disk image is
> zero-padded by `truncate`. A larger kernel would need its `_start` stub to clear
> `.bss` by hand.

## The first byte must be `_start`

The link command is in the [Makefile](../guides/building-and-running.md):

```sh
ld -m elf_i386 -T linker.ld -o kernel.bin kernel_entry.o kernel.o
```

Notice the **order of the inputs**: `kernel_entry.o` comes *before* `kernel.o`.
This is intentional and critical. Within the merged `.text` section, input
sections are placed in command-line order, so `kernel_entry.o`'s code — including
`_start` — lands at the very beginning of `.text`, which the script placed at
`0x1000`.

The chain therefore lines up perfectly:

```text
bootloader:  call 0x1000
                 |
                 v
0x1000:      _start  (from kernel_entry.o, linked first)
                 |  call kernel_main
                 v
             kernel_main()  (your C code)
```

If `kernel.o` were linked first, some arbitrary C function would sit at `0x1000`,
the bootloader's `call 0x1000` would land in the middle of the wrong code, and the
kernel would not boot.

> ⚠️ **Caveat:** `-m elf_i386` here tells `ld` how to interpret the *input* object
> files (they are 32-bit ELF `.o` files), **not** the output format. The output is
> still a flat binary because `OUTPUT_FORMAT(binary)` in the script overrides it.
> The two are not in conflict: one describes what goes in, the other what comes out.

## From `kernel.bin` to a bootable image

The linker produces `kernel.bin`, but that is only the kernel half. The
[Makefile](../guides/building-and-running.md) concatenates the boot sector and the
kernel, then pads the result:

```sh
cat boot.bin kernel.bin > helloworld-c.img
truncate -s 10240 helloworld-c.img
```

`boot.bin` is exactly 512 bytes (one sector, ending in the `0xAA55` signature),
so `kernel.bin` begins right at sector 2 — precisely where the
[bootloader's disk read](disk-loading-int13.md) (`cl = 0x02`) starts loading from.
The `truncate -s 10240` pads the image to **20 sectors** (`20 * 512 = 10240`),
guaranteeing the disk is large enough for the BIOS to read the requested sectors.

> 💡 **Tidbit:** `cat` + `truncate` is the entire "image build" step — there is no
> filesystem on this disk at all. Sector 1 is the boot code, sectors 2-onward are
> the flat kernel binary, and the rest is zero padding. The bootloader knows the
> kernel's location purely by convention (sector 2, address `0x1000`), not by
> reading any directory structure.

## See also

- [Freestanding C](freestanding-c.md) — the objects this script links, and why there's no `crt0`
- [Protected mode](protected-mode.md) — the bootloader that `call`s `0x1000`
- [Disk loading with INT 13h](disk-loading-int13.md) — how the kernel reaches memory in the first place
- [Boot sector](boot-sector.md) — the 512-byte first sector this image starts with
- [Memory map](../reference/memory-map.md) — where `0x1000`, the stack, and the GDT sit
- [`reference/toolchain-and-build.md`](../reference/toolchain-and-build.md) — the full build pipeline
- [`guides/building-and-running.md`](../guides/building-and-running.md) — running the Makefile yourself
- [`guides/debugging-with-gdb.md`](../guides/debugging-with-gdb.md) — debugging despite the stripped binary
- [Home](../Home.md)
