# MyOS-Simple Wiki

*A field guide to building a bare-metal x86 operating system from a 512-byte boot sector up to a protected-mode C system with a shell, a clock, processes, and a calculator.*

This wiki is the companion reference for [MyOS-Simple](../README.md) — a progressive,
five-stage OS tutorial. The project's `README.md` tells you *what* each stage is; this
wiki explains *how* and *why* every mechanism works, down to the byte. Every article is
checked against the actual source in this repository and cites it as `path:line`.

> 💡 **Tidbit:** Nothing in this project depends on an existing OS at runtime. From the
> first 512 bytes onward, the only thing beneath your code is the BIOS — and by Stage 2,
> not even that.


## Start here

| If you want to… | Go to |
|-----------------|-------|
| Boot something in two minutes | [Building and running](guides/building-and-running.md) |
| Follow the story stage by stage | [Stage 1](stages/stage-1-assembly-boot.md) → [2](stages/stage-2-c-protected-mode.md) → [3](stages/stage-3-interactive-shell.md) → [4](stages/stage-4-clock-processes-calc.md) → [5](stages/stage-5-release.md) |
| Understand one idea deeply | [Concepts](#concepts) below |
| Look up a number, port, or byte | [Reference](#reference) below |
| Extend the OS yourself | [Writing your own stage](guides/writing-your-own-stage.md) |


## The five stages

| # | Stage | Mode | What it adds |
|---|-------|------|--------------|
| 1 | [Assembly boot sector](stages/stage-1-assembly-boot.md) | 16-bit real | Boot, BIOS print, color + keyboard variant |
| 2 | [C kernel in protected mode](stages/stage-2-c-protected-mode.md) | 32-bit protected | Bootloader, GDT, real→protected switch, C kernel, direct VGA |
| 3 | [Interactive shell](stages/stage-3-interactive-shell.md) | 32-bit protected | Polled keyboard, command interpreter (5 commands) |
| 4 | [Clock, processes, calculator](stages/stage-4-clock-processes-calc.md) | 32-bit protected | CMOS RTC, process model, fixed-point calc, history/tab/aliases (20 commands) |
| 5 | [The stabilized release](stages/stage-5-release.md) | 32-bit protected | Consolidated command set (18), committed build artifacts |


## Concepts

The theory, one idea per page. Read these alongside the stage that introduces them.

**Firmware & boot**
- [The boot process](concepts/boot-process.md) — power-on to C entry, end to end
- [Real mode](concepts/real-mode.md) — 16-bit segment:offset, the 1 MiB world
- [BIOS services](concepts/bios-services.md) — `int 0x10`, `int 0x13`, `int 0x16`
- [The boot sector](concepts/boot-sector.md) — the 512-byte, `0xAA55` contract
- [Disk loading with int 0x13](concepts/disk-loading-int13.md) — CHS reads, sector counts

**Into protected mode**
- [Protected mode](concepts/protected-mode.md) — the switch, the flat model, the far jump
- [The Global Descriptor Table](concepts/global-descriptor-table.md) — descriptors and selectors
- [Freestanding C](concepts/freestanding-c.md) — life without a standard library
- [Linker scripts](concepts/linker-scripts.md) — placing a flat binary at `0x1000`

**Talking to hardware**
- [VGA text mode](concepts/vga-text-mode.md) — the framebuffer at `0xB8000`
- [The 8042 PS/2 keyboard](concepts/ps2-keyboard-8042.md) — polling ports `0x60`/`0x64`
- [Scancodes](concepts/scancodes.md) — Set 1, make/break, translation

**What an OS does**
- [The CMOS real-time clock](concepts/cmos-rtc.md) — wall-clock time, BCD, uptime
- [Cooperative scheduling](concepts/cooperative-scheduling.md) — PCBs, the ready queue (and what's only *modeled*)
- [Fixed-point arithmetic](concepts/fixed-point-arithmetic.md) — decimals without an FPU


## Reference

Dense, authoritative lookups.

- [Memory map](reference/memory-map.md) — every address the OS touches
- [I/O ports](reference/io-ports.md) — `0x60`, `0x64`, `0x70`, `0x71`, and the BIOS interrupts
- [GDT descriptor format](reference/gdt-descriptor-format.md) — the 8-byte descriptor, bit by bit
- [Scancode tables](reference/scancode-tables.md) — both Set-1 tables, reproduced exactly
- [Command reference](reference/command-reference.md) — every shell command across stages
- [Toolchain and build](reference/toolchain-and-build.md) — tools, flags, targets, versions
- [Glossary](reference/glossary.md) — every term, defined and linked


## Guides

- [Building and running](guides/building-and-running.md)
- [Debugging with GDB](guides/debugging-with-gdb.md)
- [Troubleshooting](guides/troubleshooting.md)
- [Writing your own stage](guides/writing-your-own-stage.md)


## How to read this wiki

The stages are a narrative; the concepts are the encyclopedia behind it. The most
rewarding path is to read a stage page, then chase its links into the concept pages
whenever you want the full theory, and keep the reference pages open in another tab.

> 💡 **Tidbit:** The hardest leap in the whole tutorial is not the C — it is *getting
> the machine into a state where C can run at all*. That is the entire job of
> [Stage 2](stages/stage-2-c-protected-mode.md): a [GDT](concepts/global-descriptor-table.md),
> the [protected-mode switch](concepts/protected-mode.md), and a
> [fixed load address](concepts/linker-scripts.md).

> ⚠️ **Caveat:** This is a teaching OS. The process scheduler is an instructive *model* —
> the register-level context switch is described but not performed — and several
> shortcuts (no A20 handling, no RTC update-race guard, fixed sector counts tuned for
> QEMU) are documented honestly on the relevant pages rather than hidden. Learn the
> shape here; learn the production details elsewhere.
