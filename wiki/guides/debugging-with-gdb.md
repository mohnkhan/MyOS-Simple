# Debugging with GDB

*Freeze the CPU at power-on, attach GDB over a socket, and single-step from the boot sector into your C kernel.*

QEMU ships a built-in GDB stub. The C stages expose it through `make debug`, so
you can watch the machine execute the boot sector in 16-bit real mode, follow the
switch into 32-bit protected mode, and step into the kernel — all from a normal
GDB session in a second terminal.

## Start QEMU with the GDB stub

In the stage directory you want to debug:

```sh
cd helloworld-os-c
make debug
```

`make debug` runs (see `helloworld-os-c/Makefile:60`):

```sh
qemu-system-x86_64 -drive format=raw,file=helloworld-c.img -s -S
```

The two flags are the whole trick:

- **`-s`** — start a GDB stub listening on TCP port **1234**. It is shorthand for
  `-gdb tcp::1234`.
- **`-S`** — freeze the CPU at reset and **do not start executing** until you
  tell it to. The QEMU window will appear black and frozen; that is correct.

> 💡 **Tidbit:** `-s` and `-S` are independent. `-s` alone gives you a stub you
> can attach to a *running* machine; `-S` alone freezes the CPU with no way in.
> You almost always want both for boot debugging.

## Attach GDB

Leave `make debug` running and open a second terminal:

```sh
gdb
```

Then inside GDB:

```text
(gdb) target remote localhost:1234
```

The CPU is sitting at the reset vector. The very first instruction executed will
be at the BIOS, which then loads your boot sector to `0x7C00`.

## The flat-binary symbol problem

The kernel is linked with `OUTPUT_FORMAT(binary)` (`helloworld-os-c/linker.ld:11`),
so the bootable image is a **flat binary with no symbol table**. GDB therefore
knows no function names — `break kernel_main` will fail, and backtraces show raw
addresses. This is the single most important thing to understand about debugging
this OS.

You have two practical options.

### Option A — debug by address

Everything in this OS lives at known fixed addresses, so you can debug entirely
with numeric breakpoints. Because the kernel is a flat binary, **`0x1000` is both
the load address and the entry point** — there is no relocation.

```text
(gdb) break *0x7c00      # boot sector entry (BIOS jumps here)
(gdb) break *0x1000      # kernel entry (boot.asm calls here)
(gdb) continue
```

### Option B — load symbols from the ELF

The linker first produces an ELF object before flattening it to a binary. If you
keep an unstripped ELF around, point GDB at it for real C symbols. The simplest
approach is to load the kernel object file at the kernel's load address:

```text
(gdb) add-symbol-file kernel.o 0x1000
```

Now `break kernel_main`, `next`, and source-level stepping work. The object file
`kernel.o` (or `shell.o` on stage 3) is left in the build directory after `make`,
so it is already there. If you want a single fully-linked symbol file, build a
`kernel.elf` with the same link line minus `OUTPUT_FORMAT(binary)` and
`add-symbol-file kernel.elf`.

> ⚠️ **Caveat:** `add-symbol-file` lays the symbols over the addresses you give
> it. Because the kernel is not relocated and loads exactly at `0x1000`, the
> symbol addresses line up. If you ever change the load address in `linker.ld`
> *and* `boot.asm`, update the `add-symbol-file` offset to match.

## Set the right architecture for the CPU mode

GDB does not automatically know whether the CPU is in 16-bit real mode or 32-bit
protected mode, and it will disassemble incorrectly if you guess wrong. Set it
explicitly:

```text
(gdb) set architecture i8086      # before the protected-mode switch (boot sector)
(gdb) set architecture i386       # after the far jump into protected mode
```

The switch happens in `boot.asm` when `CR0.PE` is set and the far jump executes
(`helloworld-os-c/boot.asm:42-45`). Step over that far jump, then switch GDB to
`i386` and the kernel will disassemble correctly.

## A worked session

Debugging stage 2 from the boot sector through the protected-mode switch and into
the C kernel:

```sh
# Terminal 1
cd helloworld-os-c
make debug
```

```text
# Terminal 2
$ gdb
(gdb) target remote localhost:1234
(gdb) set architecture i8086
(gdb) break *0x7c00
(gdb) continue
Breakpoint 1, 0x00007c00 in ?? ()

(gdb) x/10i $pc          # disassemble the boot sector entry
(gdb) info registers     # check DS/ES/SS/SP setup

# step until just past the far jump that enters protected mode, then:
(gdb) set architecture i386
(gdb) break *0x1000      # kernel entry
(gdb) continue
Breakpoint 2, 0x00001000 in ?? ()

(gdb) add-symbol-file kernel.o 0x1000   # optional: real C symbols
(gdb) layout asm          # live disassembly view
(gdb) x/20i $pc
(gdb) stepi               # single-step one instruction
```

## The commands you will actually use

| Command | What it does |
|---------|--------------|
| `target remote localhost:1234` | Attach to the QEMU stub |
| `set architecture i8086` / `i386` | Match real / protected mode |
| `break *0x7c00` | Stop at boot-sector entry |
| `break *0x1000` | Stop at kernel entry |
| `continue` (`c`) | Resume execution |
| `stepi` (`si`) | Execute one instruction |
| `x/20i $pc` | Disassemble 20 instructions at the program counter |
| `info registers` | Dump all CPU registers |
| `layout asm` | TUI live disassembly pane |
| `add-symbol-file kernel.o 0x1000` | Overlay C symbols on the flat binary |

## QEMU monitor — the no-GDB alternative

If you only need to inspect state, skip GDB and use the QEMU monitor. Start QEMU
with `-monitor stdio` (no `-s -S` needed):

```sh
qemu-system-x86_64 -drive format=raw,file=helloworld-c.img -monitor stdio
```

Then at the `(qemu)` prompt:

```text
(qemu) info registers
(qemu) xp/20i 0x7c00      # disassemble physical memory at the boot sector
(qemu) xp/16xb 0xb8000    # peek at VGA text memory
```

`xp` reads *physical* memory, which is what you want with paging disabled.

> 💡 **Tidbit:** A reboot loop in QEMU is almost always a **triple fault** — a
> fault raised while handling a fault while handling a fault, which forces the
> CPU to reset. It is the classic symptom of a broken GDT, a bad stack, or a
> truncated kernel. Set `break *0x1000`, step into the kernel, and watch where
> execution derails. See [troubleshooting.md](troubleshooting.md).

## See also

- [building-and-running.md](building-and-running.md) — what `make debug` builds
- [troubleshooting.md](troubleshooting.md) — diagnosing crashes and reboot loops
- [writing-your-own-stage.md](writing-your-own-stage.md) — when your new code crashes
- [../concepts/protected-mode.md](../concepts/protected-mode.md) — the real-to-protected switch
- [../concepts/global-descriptor-table.md](../concepts/global-descriptor-table.md) — what a bad GDT looks like
- [../reference/memory-map.md](../reference/memory-map.md) — the fixed addresses to break on
- [../Home.md](../Home.md) — wiki home
