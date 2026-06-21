[← Home](../Home.md)

# Toolchain and Build

*The exact tools, flags, and Make targets that turn source into a bootable disk image.*

MyOS-Simple builds a freestanding 32-bit (`i386`) image with a small, standard toolchain
and no cross-compiler. This page is the authoritative reference for the tools, the exact
flags, the build pipeline, and every Make target. For step-by-step instructions see the
[building-and-running guide](../guides/building-and-running.md).

## Required tools

| Tool | Role | Stage Makefile invocation |
|------|------|---------------------------|
| `nasm` | Assemble the boot sector (flat binary) and the 32-bit kernel entry stub | `nasm -f bin` / `nasm -f elf32` |
| `gcc` | Compile the [freestanding C](../concepts/freestanding-c.md) kernel (needs `-m32` multilib) | `gcc $(CFLAGS)` |
| `ld` (binutils) | Link the kernel to its fixed load address | `ld -m elf_i386 -T linker.ld` |
| `make` | Drive each stage's build | — |
| coreutils | `cat` to concatenate, `truncate` to pad the image | `cat … > img; truncate -s …` |
| `qemu-system-x86` | Boot the raw disk image | `qemu-system-x86_64 -drive …` |

### Tested versions

The tree is verified known-good against these versions; older/newer generally work.

| Component | Version tested |
|-----------|----------------|
| nasm | 2.16.01 |
| gcc | 13.3.0 (with `-m32` multilib) |
| ld | 2.42 (GNU Binutils) |
| qemu | 8.2.2 (`qemu-system-x86_64`) |
| make | 4.3 |

## Compiler flags (`CFLAGS`)

From each C stage's `Makefile`:

```sh
-m32 -ffreestanding -fno-pic -fno-pie -nostdlib -nostdinc \
-fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs \
-Wall -Wextra -c
```

| Flag | Why |
|------|-----|
| `-m32` | Emit 32-bit (i386) code, not 64-bit |
| `-ffreestanding` | No hosted C runtime; `main` is not special — see [freestanding C](../concepts/freestanding-c.md) |
| `-fno-pic` / `-fno-pie` | No position-independent code; the kernel runs at a fixed address |
| `-nostdlib` / `-nodefaultlibs` | Do not link the standard library |
| `-nostdinc` | Do not search system include paths (no [libc](glossary.md#libc) headers) |
| `-fno-builtin` | Do not assume libc semantics for `memcpy`, `strlen`, etc. |
| `-fno-stack-protector` | No `__stack_chk_*` symbols (there is no libc to provide them) |
| `-nostartfiles` | No [`crt0`](glossary.md#crt0-startup) startup objects; the asm stub is the entry |
| `-Wall -Wextra` | Maximum warnings |
| `-c` | Compile to object only; link separately |

## Assembler and linker invocations

| Step | Command | Output |
|------|---------|--------|
| Boot sector | `nasm -f bin boot.asm -o boot.bin` | Flat binary (no headers) |
| Kernel entry stub | `nasm -f elf32 kernel_entry.asm -o kernel_entry.o` | ELF32 object |
| C sources | `gcc $(CFLAGS) file.c -o file.o` | ELF32 objects |
| Link | `ld -m elf_i386 -T linker.ld -o kernel.bin *.o` | Kernel binary at load address |

The boot sector is `-f bin` (raw bytes the BIOS executes directly), while the kernel is
ELF32 so the [linker script](../concepts/linker-scripts.md) can place sections.

## Building the disk image

```sh
cat boot.bin kernel.bin > image.img      # boot sector first, then kernel
truncate -s <SIZE> image.img             # pad to a whole number of sectors
```

| Stage | Directory | Sectors loaded by `boot.asm` | `truncate -s` |
|-------|-----------|------------------------------|---------------|
| 2 | `helloworld-os-c` | 16 | 10240 (20 sectors) |
| 3 | `os-c-with-shell` | 15 | 10240 (20 sectors) |
| 4 | `helloworld-os-c-v2` | 39 | 20480 (40 sectors) |
| 5 | `helloworld-os-c-v3` | 39 | 20480 (40 sectors) |

The boot sector sits at [`0x7C00`](memory-map.md); the kernel is read to `0x1000`
starting at [CHS sector 2](../concepts/disk-loading-int13.md) (sector numbering is
1-based, so the boot sector is sector 1 and the kernel begins at sector 2).

## The build pipeline

```text
boot.asm  --nasm -f bin-->  boot.bin   --+
                                         |--cat-->  image.img  --truncate-->  padded image
kernel_entry.asm --nasm -f elf32--+      |
*.c  --gcc -m32 -c-->  *.o  -------+--ld-->  kernel.bin --+
```

## Make targets

| Target | Effect | Stages |
|--------|--------|--------|
| `make` | Build the bootable image(s) | All |
| `make run` | Boot the primary image: `qemu-system-x86_64 -drive format=raw,file=IMG` | All |
| `make run-simple` | Boot the pure-assembly "simple" image | C stages (2–5) |
| `make run-color` | Boot the color/keyboard variant | Stage 1 only |
| `make debug` | Boot under QEMU with a [GDB stub](glossary.md#gdb-stub): adds `-s -S` (TCP `:1234`, CPU halted at reset) | C stages (2–5) |
| `make clean` | Remove build artifacts (`*.bin *.o *.img`) | All |
| `make help` | List available targets | All |

### What `-s -S` means

| Flag | Meaning |
|------|---------|
| `-s` | Shorthand for `-gdb tcp::1234` — open a GDB stub on port 1234 |
| `-S` | Freeze the CPU at reset; do not start executing until a debugger says `continue` |

Connect with `gdb`, then `target remote :1234`. See the
[debugging guide](../guides/debugging-with-gdb.md).

## License

MyOS-Simple is released under the **MIT License** (see `LICENSE`).

> 💡 **Tidbit:** `truncate -s 10240` produces exactly 20 sectors of 512 bytes. The pad
> matters because the bootloader's `int 0x13` read asks for a fixed sector count; if the
> image file is shorter than that, QEMU's emulated disk read can fail or return zeros for
> the missing tail.

> ⚠️ **Caveat:** On a 64-bit host, plain `gcc` cannot produce a working `-m32` object
> without the 32-bit multilib (`gcc-multilib` on Debian/Ubuntu, `glibc-devel.i686` on
> Fedora, `lib32-glibc` on Arch). Missing it yields cryptic `bits/libc-header-start.h`
> or linker errors — see [troubleshooting](../guides/troubleshooting.md).

> 💡 **Tidbit:** The boot sector is assembled with `-f bin`, *not* ELF, because the BIOS
> loads and jumps to raw bytes — there is no loader to parse an ELF header at `0x7C00`.
> The kernel can afford ELF only because `ld` resolves it into a flat layout first.

## See also

- [Building and running](../guides/building-and-running.md) — the how-to guide
- [Debugging with GDB](../guides/debugging-with-gdb.md) — using `make debug`
- [Troubleshooting](../guides/troubleshooting.md) — multilib and image-size errors
- [Linker scripts](../concepts/linker-scripts.md) — `linker.ld` and section placement
- [Freestanding C](../concepts/freestanding-c.md) — why the flags above
- [Disk loading via int 0x13](../concepts/disk-loading-int13.md) — sectors and CHS
- [Memory map](memory-map.md) — where the loaded image lands
- [Glossary](glossary.md)
- [Home](../Home.md)
