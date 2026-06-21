# Freestanding C

*Writing C with no operating system underneath it — no libc, no headers, no `main`, no safety net.*

When you write an ordinary C program, an enormous amount of machinery is hidden
from you. The C standard library (`printf`, `malloc`, `memcpy`, …) is linked in.
Standard headers like `<stdio.h>` and `<stdint.h>` are available. A startup
routine (`crt0`) runs *before* `main`, sets up the stack and arguments, and calls
your code. None of that exists when your program **is** the operating system.

MyOS-Simple's C kernels are compiled in **freestanding** mode, which strips all of
that away. This page explains what freestanding means, what the compiler flags in
the [Makefile](../guides/building-and-running.md) actually do, and how the project
fills the resulting gaps by hand.

## Hosted vs. freestanding

The C standard formally distinguishes two execution environments:

- **Hosted** — the normal case. A full standard library is present, the program
  starts at `main`, and the whole library is available.
- **Freestanding** — the minimal case. Only a handful of headers are guaranteed
  (`<stddef.h>`, `<stdint.h>`, `<limits.h>`, …), the entry point is
  implementation-defined, and there is *no* standard library implementation. This
  is the mode used for kernels, firmware, and embedded targets.

MyOS-Simple is firmly in the freestanding camp: the kernel runs in
[protected mode](protected-mode.md) on bare metal, with nothing beneath it but the
CPU and the hardware.

## The compiler flags

The kernel's compilation flags come straight from the
[Makefile](../guides/building-and-running.md) (`CFLAGS`):

```sh
gcc -m32 -ffreestanding -fno-pic -fno-pie -nostdlib -nostdinc \
    -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs \
    -Wall -Wextra -c kernel.c -o kernel.o
```

Each flag exists to remove an assumption the compiler would otherwise make about
running under an OS:

| Flag | What it does |
|------|--------------|
| `-m32` | Generate 32-bit i386 code (matches the protected-mode target). |
| `-ffreestanding` | Tell GCC the program is freestanding: no hosted-environment assumptions, no implicit `main` semantics. |
| `-fno-pic` / `-fno-pie` | Disable position-independent code. The kernel is loaded at a fixed address (`0x1000`) and uses absolute addresses. |
| `-nostdlib` | Do not link the standard library or startup files. |
| `-nostdinc` | Do not search the standard system include directories. |
| `-fno-builtin` | Do not assume libc functions exist or behave as built-ins (so GCC won't, say, replace a loop with a call to `memset`). |
| `-fno-stack-protector` | Remove stack canary checks, which call into a runtime that isn't present. |
| `-nostartfiles` | Do not link the C runtime startup objects (`crt0`/`crt1`). There is no automatic call to `main`. |
| `-nodefaultlibs` | Do not link any default libraries. |
| `-Wall -Wextra` | Enable thorough warnings — important when you have no safety net. |
| `-c` | Compile only; linking is done separately by the [linker script](linker-scripts.md). |

> ⚠️ **Caveat:** `-nostdinc` means `#include <stdint.h>` would normally **fail** —
> the system header path is gone. Stages 4 and 5 of the project therefore ship
> their *own* minimal headers next to the source (e.g.
> [`helloworld-os-c-v3/stdint.h`](../reference/toolchain-and-build.md)), defining
> exactly the typedefs the kernel needs:
>
> ```c
> typedef unsigned char  uint8_t;
> typedef unsigned short uint16_t;
> typedef unsigned int   uint32_t;
> typedef unsigned int   size_t;
> ```

## No startup code: the `_start` stub

In a hosted program, `crt0` runs first and eventually calls `main`. With
`-nostartfiles`, there is no `crt0`. The project supplies the entry point itself
in [`kernel_entry.asm`](linker-scripts.md):

```asm
[BITS 32]
[EXTERN kernel_main]

global _start

_start:
    call kernel_main
    jmp $
```

That is the *entire* C runtime for this kernel. There is no argument marshalling,
no environment setup, no global constructors — just a `call` into C, and an
infinite `jmp $` halt if `kernel_main` ever returns. The stack was already set up
by the [bootloader](protected-mode.md) (`esp = 0x90000`) before this code ran.

> 💡 **Tidbit:** The C entry point is named `kernel_main` (or `shell_main` in the
> shell stage), **not** `main`. The name `main` is only special to the *hosted*
> startup code, which we don't have. Here, the entry point is whatever `_start`
> chooses to call — see [`shell.c:305`](../stages/stage-3-interactive-shell.md),
> where `kernel_main` just forwards to `shell_main`.

## No standard library: rolling your own

With `-nostdlib` and `-fno-builtin`, none of the familiar conveniences exist:

- **No `printf`** — output is produced by writing `(char, attribute)` pairs
  directly into [VGA text memory](vga-text-mode.md) at `0xB8000`.
- **No `malloc`** — there is no heap; all buffers are fixed-size locals or globals.
- **No `<string.h>`** — string helpers are written by hand.

For example, [`shell.c:121-137`](../stages/stage-3-interactive-shell.md) provides
its own `strcmp` and `strncmp`, used to match typed commands against the built-in
command table:

```c
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, int n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}
```

## Talking to hardware: inline assembly

A freestanding kernel still has to reach the hardware, and there is no library
wrapper to do it. MyOS-Simple uses GCC **inline assembly** for the two primitives
it needs: port I/O and memory-mapped I/O.

### Port I/O: `inb` and `outb`

The keyboard controller and other devices are read and written through x86 **I/O
ports**, which require the `in`/`out` instructions — something C has no operator
for. The project wraps them in tiny inline-asm functions. From
[`shell.c:38-42`](ps2-keyboard-8042.md):

```c
unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}
```

The corresponding writer, used elsewhere in the project, is:

```c
void outb(unsigned short port, unsigned char data) {
    __asm__ __volatile__("outb %0, %1" : : "a"(data), "Nd"(port));
}
```

The constraints are worth understanding: `"=a"` puts the result in `AL`/`AX`,
`"a"` supplies the source from `AL`, and `"Nd"` lets the port be an 8-bit
immediate when small or `DX` otherwise — exactly matching how the `in`/`out`
instructions encode their operands.

> 💡 **Tidbit:** `__volatile__` tells GCC **not** to optimize the asm away or
> reorder it relative to other volatile accesses. Hardware I/O has side effects the
> compiler cannot see, so without `volatile` an "unused" `inb` could be deleted, or
> two reads could be collapsed into one — breaking, for example, the keyboard
> status poll in [`shell.c:145`](ps2-keyboard-8042.md).

### Memory-mapped I/O: `volatile char*`

VGA text output is **memory-mapped** rather than port-based: the screen is a region
of RAM starting at `0xB8000`. The kernel writes to it through a `volatile`
pointer, as in [`kernel.c:51-56`](vga-text-mode.md):

```c
void putchar_at(char c, int x, int y, char attr) {
    volatile char* video = (volatile char*)VIDEO_MEMORY;   /* 0xB8000 */
    int offset = (y * 80 + x) * 2;
    video[offset] = c;
    video[offset + 1] = attr;
}
```

> 💡 **Tidbit:** The `volatile` qualifier is essential here too. To the compiler,
> writing a character to `0xB8000` and never reading it back looks like a **dead
> store** it is free to remove. `volatile` forces every write to actually happen,
> because the side effect — a glyph appearing on screen — is invisible to the
> optimizer.

## Why a flat binary, not ELF

A freestanding kernel also can't rely on an ELF loader to place its sections in
memory — the [bootloader](protected-mode.md) just copies raw bytes to `0x1000`
and jumps. That requirement is what drives the
[linker script](linker-scripts.md) to emit `OUTPUT_FORMAT(binary)` and to link the
entry stub first. See that page for the full story.

## See also

- [Linker scripts](linker-scripts.md) — how the freestanding objects become a flat binary at `0x1000`
- [Protected mode](protected-mode.md) — the environment the kernel runs in
- [VGA text mode](vga-text-mode.md) — the memory-mapped display the kernel writes to
- [PS/2 keyboard (8042)](ps2-keyboard-8042.md) — the port-mapped device `inb` reads
- [`reference/io-ports.md`](../reference/io-ports.md) — the I/O ports the kernel touches
- [`reference/toolchain-and-build.md`](../reference/toolchain-and-build.md) — every tool and flag in the build
- [Stage 2: C in protected mode](../stages/stage-2-c-protected-mode.md) — where C first appears
- [Home](../Home.md)
