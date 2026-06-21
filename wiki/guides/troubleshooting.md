# Troubleshooting

*Every failure mode in MyOS-Simple, what it actually means, and how to fix it.*

Bare-metal failures are blunt: a blank screen, a reboot loop, or a one-line error
from the BIOS. The good news is that with no OS underneath, the list of things
that can go wrong is short and the symptoms are distinctive. This guide maps each
symptom to its cause and fix.

## Build fails

### `nasm: command not found` (or `gcc`, `ld`, `make`, `qemu-system-x86_64`)

The toolchain is not installed. Install it for your distro — see
[building-and-running.md](building-and-running.md#install-the-toolchain). Quick
version:

```sh
# Debian/Ubuntu
sudo apt install nasm gcc gcc-multilib binutils make qemu-system-x86
```

### `fatal error: bits/libc-header-start.h: No such file or directory`

### `gcc: error: ... cannot find -lgcc` / `/usr/bin/ld: cannot find ...i386...`

These all mean the same thing: you are on a **64-bit host without 32-bit
multilib**, so `gcc -m32` cannot find the 32-bit headers or support libraries.
The kernel is compiled as 32-bit (`-m32`, see `helloworld-os-c/Makefile:9`), so
this is required.

Install the multilib package for your distro:

```sh
sudo apt install gcc-multilib                       # Debian/Ubuntu
sudo dnf install glibc-devel.i686 libgcc.i686       # Fedora
sudo pacman -S lib32-glibc                          # Arch
```

Then `make clean && make`.

> 💡 **Tidbit:** Despite `-nostdlib -nostdinc`, GCC still needs the 32-bit
> *compiler* support files (start files and `libgcc`) to target i386 at all. The
> freestanding flags drop the C library, not the architecture support.

## "Disk read error!"

You booted and the screen shows `Disk read error!` and nothing else.

This string is printed by the bootloader's `disk_error` handler
(`helloworld-os-c/boot.asm:83-86`). It runs when the BIOS `int 0x13` disk read
returns the **carry flag set** (`jc disk_error`, `boot.asm:77`) — the BIOS could
not read the sectors the bootloader asked for.

Causes and fixes, in order of likelihood:

1. **Image too small / not padded.** The bootloader reads a fixed number of whole
   sectors; if the image file is shorter than that, the read fails. The Makefile
   pads with `truncate` (`helloworld-os-c/Makefile:44`). If you edited the build,
   make sure the image is still padded to at least boot sector + N sectors.
2. **Sector count larger than the image.** If you raised `mov dh, N` in
   `boot.asm` but did not raise the `truncate` size, the BIOS is asked to read
   past the end of the file. Bump both together.
3. **You are booting the wrong file.** Make sure you are pointing QEMU at the
   built `.img`, not a stray `.bin`.

> ⚠️ **Caveat:** The BIOS reads **whole 512-byte sectors**. An image that is not
> a whole number of sectors, or is a few bytes short of the requested count, can
> trigger this error even when "it looks big enough." `truncate` exists precisely
> to round the file up to a clean sector boundary.

## Blank screen, or QEMU reboots in a loop

The boot sector ran (no disk error), but you get a black screen, or QEMU keeps
resetting over and over. This is almost always a **triple fault**: the CPU hit a
fault, faulted again trying to handle it, faulted a third time, and reset. With
no exception handlers installed, a triple fault is the default outcome of almost
any serious error.

The usual root causes:

1. **Kernel truncated.** The bootloader loaded fewer sectors than the kernel
   actually occupies, so execution runs off the end into garbage. This is the
   most common cause after you add code. See
   [Kernel grew too large](#kernel-grew-too-large).
2. **Bad GDT.** A malformed descriptor or wrong `lgdt` makes the far jump into
   protected mode land somewhere invalid. See
   [../concepts/global-descriptor-table.md](../concepts/global-descriptor-table.md).
3. **Wrong load/entry address.** The kernel must load to `0x1000` and the linker
   must place `.text` there (`helloworld-os-c/linker.ld:15`). A mismatch means
   the `call 0x1000` (`boot.asm:61`) jumps into nothing.

Debug it by attaching GDB, breaking at `*0x1000`, and single-stepping to see
where execution derails — see
[debugging-with-gdb.md](debugging-with-gdb.md#qemu-monitor--the-no-gdb-alternative).

### Kernel grew too large

The bootloader loads a **fixed number of sectors**, hard-coded per stage:

| Stage | Sectors | Source |
|-------|---------|--------|
| 2 | 16 | `helloworld-os-c/boot.asm:35` |
| 3 | 15 | `os-c-with-shell/boot.asm:34` |
| 4 | 39 | `helloworld-os-c-v2/boot.asm:35` |
| 5 | 39 | `helloworld-os-c-v3/boot.asm:35` |

If your kernel exceeds N × 512 bytes, the tail is **silently dropped** — no
error, just a crash when execution reaches the missing code. Check the kernel
size:

```sh
ls -l kernel.bin        # must be <= N * 512 bytes
```

If it is too big, raise the sector count in `boot.asm` and raise the `truncate`
size in the `Makefile` to match. Full procedure in
[writing-your-own-stage.md](writing-your-own-stage.md#growing-the-kernel-safely).

## Keyboard does nothing

You type and nothing appears. On the C stages, keyboard input is **polled
directly** from the 8042 controller — there is no BIOS input in protected mode.
The code waits for the status port `0x64` output-full bit, then reads the
scancode from data port `0x60` (`os-c-with-shell/shell.c:145-148`).

Things to check:

1. **Key releases are ignored by design.** Every key sends a press scancode and,
   on release, the same code with the high bit set. The handler returns `0` for
   releases (`shell.c:151-159`). So only *presses* produce output — that is
   correct behavior, not a bug.
2. **The QEMU window must have focus.** Click into it before typing.
3. **Unmapped keys produce nothing.** Only scancodes below 90 are translated
   (`shell.c:168`), and only printable ASCII 32–126 is echoed
   (`shell.c:201`). Function keys, arrows, etc. are dropped on the early stages.

## Unknown command

The shell prints `Unknown command: <name>` in red. The command was not matched by
the dispatcher. On stage 3 the dispatcher is a chain of `strcmp` comparisons in
`shell_main` (`os-c-with-shell/shell.c:287-300`); the recognized commands are
`help`, `clear`, `echo`, `about`, `shutdown`. Type `help` to see the list. To add
your own command, see
[writing-your-own-stage.md](writing-your-own-stage.md#add-a-shell-command).

## It boots in QEMU but not on real hardware

The images are tuned for QEMU and make assumptions that real firmware may not
share:

- **A20 line.** The bootloader does not explicitly enable the A20 gate. QEMU
  usually has it enabled; some real BIOSes do not, which corrupts access above
  1 MiB.
- **Sector counts and CHS geometry.** The disk read uses BIOS `int 0x13` CHS
  addressing assuming the kernel is contiguous from sector 2
  (`helloworld-os-c/boot.asm:70-76`). Real media may need different geometry or a
  different sector count.

These are documented limitations rather than bugs; treat QEMU as the reference
target.

> 💡 **Tidbit:** `truncate` only sets the file's *length* — it does not write any
> meaningful data. It exists solely so the image is a whole number of 512-byte
> sectors and is long enough for the BIOS to read every sector the bootloader
> requests.

## Quick symptom → cause table

| Symptom | Most likely cause | Fix |
|---------|-------------------|-----|
| `command not found` at build | Toolchain missing | Install nasm/gcc/ld/make/qemu |
| `bits/...` / `cannot find -lgcc` | No 32-bit multilib | Install gcc-multilib / glibc-devel.i686 / lib32-glibc |
| `Disk read error!` | Image too small or sector count too high | Pad image / align `dh` and `truncate` |
| Blank screen or reboot loop | Triple fault: truncated kernel, bad GDT, wrong address | Check `kernel.bin` size; debug at `*0x1000` |
| Crash after adding code | Kernel exceeded loaded sectors | Raise `mov dh, N` and `truncate` |
| Typing does nothing | Window unfocused, or release/unmapped key | Focus window; presses only |
| `Unknown command` | Not in the dispatcher | `help`; add a command |
| Works in QEMU, not on metal | A20 / sector geometry | Documented limitation |

## See also

- [building-and-running.md](building-and-running.md) — toolchain and build internals
- [debugging-with-gdb.md](debugging-with-gdb.md) — step through a crash
- [writing-your-own-stage.md](writing-your-own-stage.md) — grow the kernel without breaking it
- [../concepts/disk-loading-int13.md](../concepts/disk-loading-int13.md) — how the BIOS read works
- [../concepts/global-descriptor-table.md](../concepts/global-descriptor-table.md) — GDT faults
- [../concepts/ps2-keyboard-8042.md](../concepts/ps2-keyboard-8042.md) — why releases are ignored
- [../reference/memory-map.md](../reference/memory-map.md) — the fixed addresses
- [../Home.md](../Home.md) — wiki home
