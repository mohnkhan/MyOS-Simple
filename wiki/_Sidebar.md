### [MyOS-Simple Wiki](Home.md)

**Stages**
- [1 · Assembly boot](stages/stage-1-assembly-boot.md)
- [2 · C protected mode](stages/stage-2-c-protected-mode.md)
- [3 · Interactive shell](stages/stage-3-interactive-shell.md)
- [4 · Clock / processes / calc](stages/stage-4-clock-processes-calc.md)
- [5 · Stabilized release](stages/stage-5-release.md)

**Concepts — boot**
- [Boot process](concepts/boot-process.md)
- [Real mode](concepts/real-mode.md)
- [BIOS services](concepts/bios-services.md)
- [Boot sector](concepts/boot-sector.md)
- [Disk loading (int 0x13)](concepts/disk-loading-int13.md)

**Concepts — protected mode**
- [Protected mode](concepts/protected-mode.md)
- [GDT](concepts/global-descriptor-table.md)
- [Freestanding C](concepts/freestanding-c.md)
- [Linker scripts](concepts/linker-scripts.md)

**Concepts — hardware**
- [VGA text mode](concepts/vga-text-mode.md)
- [8042 keyboard](concepts/ps2-keyboard-8042.md)
- [Scancodes](concepts/scancodes.md)

**Concepts — OS services**
- [CMOS RTC](concepts/cmos-rtc.md)
- [Cooperative scheduling](concepts/cooperative-scheduling.md)
- [Fixed-point arithmetic](concepts/fixed-point-arithmetic.md)

**Reference**
- [Memory map](reference/memory-map.md)
- [I/O ports](reference/io-ports.md)
- [GDT descriptor format](reference/gdt-descriptor-format.md)
- [Scancode tables](reference/scancode-tables.md)
- [Command reference](reference/command-reference.md)
- [Toolchain & build](reference/toolchain-and-build.md)
- [Glossary](reference/glossary.md)

**Guides**
- [Building & running](guides/building-and-running.md)
- [Debugging with GDB](guides/debugging-with-gdb.md)
- [Troubleshooting](guides/troubleshooting.md)
- [Writing your own stage](guides/writing-your-own-stage.md)
