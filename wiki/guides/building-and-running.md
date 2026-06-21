# Building and Running

*From a clean checkout to a booting OS in two commands — for any of the five stages.*

Every stage of MyOS-Simple is a **self-contained directory with its own `Makefile`**.
There is no top-level build; you `cd` into a stage and build it there. All five
stages share the same verbs (`make`, `make run`, `make clean`, `make help`), so
once you can build one you can build them all.

## Install the toolchain

The build is a freestanding **32-bit (`i386`)** toolchain plus an emulator. On a
64-bit host you also need the **32-bit multilib** for GCC — without it the kernel
compile fails (see [troubleshooting.md](troubleshooting.md)).

Debian / Ubuntu:

```sh
sudo apt update
sudo apt install nasm gcc gcc-multilib binutils make qemu-system-x86
```

Fedora:

```sh
sudo dnf install nasm gcc glibc-devel.i686 libgcc.i686 binutils make qemu-system-x86
```

Arch:

```sh
sudo pacman -S nasm gcc lib32-glibc binutils make qemu-system-x86
```

What each piece does:

- **nasm** — assembles the boot sector (`-f bin`, a flat binary) and the 32-bit
  kernel entry stub (`-f elf32`).
- **gcc** — compiles the C kernel as freestanding 32-bit code (`-m32`).
- **ld** (binutils) — links the kernel to a fixed load address via `linker.ld`.
- **make** — drives each stage's build.
- **coreutils** — `cat` concatenates boot sector + kernel; `truncate` pads the
  image to a whole number of sectors.
- **qemu-system-x86_64** — boots the raw disk image.

> 💡 **Tidbit:** The emulator binary is `qemu-system-x86_64` even though the OS
> runs in 32-bit protected mode. The 64-bit QEMU happily executes 32-bit guests;
> the name refers to the host target QEMU was built for, not the guest CPU.

### Tested versions

The tree is verified known-good against these versions. Older or newer usually
work too — these are just the exact ones used to validate the build:

| Component | Version |
|-----------|---------|
| nasm      | 2.16.01 |
| gcc       | 13.3.0 (with `-m32` multilib) |
| ld        | 2.42 (GNU Binutils) |
| qemu      | 8.2.2 (`qemu-system-x86_64`) |
| make      | 4.3 |

Check yours:

```sh
nasm --version && gcc --version | head -1 && ld --version | head -1
qemu-system-x86_64 --version | head -1 && make --version | head -1
```

## Build and run a single stage

Pick a stage directory and use the shared verbs:

```sh
cd helloworld-os-asm     # or any other stage directory
make                     # build the bootable image(s)
make run                 # boot the primary image in QEMU
make clean               # remove build artifacts
make help                # list available targets
```

The five stage directories are:

| Stage | Directory | What `make run` boots |
|-------|-----------|------------------------|
| 1 | `helloworld-os-asm`  | Monochrome boot-sector "Hello World" |
| 2 | `helloworld-os-c`    | First C kernel in protected mode |
| 3 | `os-c-with-shell`    | The interactive command shell |
| 4 | `helloworld-os-c-v2` | Shell + clock + processes + calculator |
| 5 | `helloworld-os-c-v3` | The stabilized release |

### Extra targets on the C stages

Stages 2–5 each add two more verbs:

```sh
make run-simple          # boot the pure-assembly "simple" image
make debug               # boot under QEMU with a GDB stub (-s -S) on :1234
```

`make debug` is the entry point for [debugging-with-gdb.md](debugging-with-gdb.md).

### Stage 1 is different

Stage 1 (`helloworld-os-asm`) is pure assembly with no C kernel, so it has no
`run-simple` or `debug` target. Instead it adds a **color/keyboard variant**:

```sh
cd helloworld-os-asm
make run            # monochrome variant (helloworld.img)
make run-color      # color + keyboard variant (helloworld_color.img)
make rebuild        # clean and rebuild both images
```

## Build every stage in one pass

To rebuild the whole tree from clean — useful after pulling new changes:

```sh
for d in helloworld-os-asm helloworld-os-c os-c-with-shell \
         helloworld-os-c-v2 helloworld-os-c-v3; do
    make -C "$d" clean && make -C "$d"
done
```

`make -C <dir>` runs make in that directory without changing your shell's
working directory, so you can launch this from the repository root.

## What `make` actually does

For the C stages the default target builds two images: the C image and a
pure-assembly "simple" image. The C image is assembled from three pieces. Using
stage 2 (`helloworld-os-c/Makefile`) as the reference:

1. **Boot sector** — `nasm -f bin boot.asm -o boot.bin`
   (`helloworld-os-c/Makefile:26`). A flat 512-byte binary ending in the
   `0xAA55` signature.
2. **Kernel entry stub** — `nasm -f elf32 kernel_entry.asm -o kernel_entry.o`
   (`Makefile:30`). The tiny assembly shim that calls the C entry point.
3. **C kernel** — `gcc -m32 -ffreestanding -nostdlib ... -c kernel.c -o kernel.o`
   (`Makefile:9-11, 34`). Freestanding, no C runtime.
4. **Link** — `ld -m elf_i386 -T linker.ld -o kernel.bin kernel_entry.o kernel.o`
   (`Makefile:38`). The linker script fixes the load address.
5. **Assemble image** — `cat boot.bin kernel.bin > helloworld-c.img` then
   `truncate -s 10240 helloworld-c.img` (`Makefile:42-44`). The boot sector sits
   first, the kernel follows, and the image is padded to a whole number of
   sectors.

Stages 3–5 follow the same recipe but link more object files. Stage 3 links
`shell.o` (`os-c-with-shell/Makefile:38`); stages 4 and 5 also link `process.o`
and `rtc.o` (`helloworld-os-c-v2/Makefile:45-46`).

> 💡 **Tidbit:** The C stages 4 and 5 `truncate -s 20480` (40 sectors) instead of
> 10240 (`helloworld-os-c-v2/Makefile:52`), because their kernels are large
> enough to need it. See [Why image size matters](#why-image-size-matters).

> ⚠️ **Caveat:** The kernel is linked with `OUTPUT_FORMAT(binary)` (see
> `helloworld-os-c/linker.ld:11`), so the output is a **flat binary with no
> symbols**. That is what makes the image directly bootable, but it also means
> GDB sees no function names by default — see
> [debugging-with-gdb.md](debugging-with-gdb.md) for how to get C symbols back.

## Run the image by hand

`make run` is just a thin wrapper. The underlying command is always:

```sh
qemu-system-x86_64 -drive format=raw,file=helloworld-c.img
```

Substitute whichever `.img` you want to boot. Running it yourself is handy when
you want to add QEMU flags, e.g. `-monitor stdio` to drop into the QEMU monitor:

```sh
qemu-system-x86_64 -drive format=raw,file=helloworld-c.img -monitor stdio
```

To quit QEMU, close the window, or press **Ctrl-A** then **X** in a text console.

## Why image size matters

The bootloader reads a **fixed number of sectors** from disk into memory — it
does not parse a filesystem or read until EOF. Each stage's `boot.asm` hard-codes
the count in `mov dh, N`:

| Stage | Sectors loaded | Source |
|-------|----------------|--------|
| 2 | 16 | `helloworld-os-c/boot.asm:35` |
| 3 | 15 | `os-c-with-shell/boot.asm:34` |
| 4 | 39 | `helloworld-os-c-v2/boot.asm:35` |
| 5 | 39 | `helloworld-os-c-v3/boot.asm:35` |

Two consequences:

- The image file must be **at least** boot sector + N sectors long, which is why
  the Makefile pads it with `truncate`. The BIOS reads whole 512-byte sectors, so
  the file must be a whole number of them.
- If you grow the kernel past N sectors it will be **silently truncated** and the
  machine will crash. Growing the kernel safely is covered in
  [writing-your-own-stage.md](writing-your-own-stage.md).

## See also

- [debugging-with-gdb.md](debugging-with-gdb.md) — step through the boot sector and kernel
- [troubleshooting.md](troubleshooting.md) — when the build or boot fails
- [writing-your-own-stage.md](writing-your-own-stage.md) — extend the OS yourself
- [../reference/toolchain-and-build.md](../reference/toolchain-and-build.md) — every flag and target
- [../concepts/boot-process.md](../concepts/boot-process.md) — power-on to C entry
- [../concepts/freestanding-c.md](../concepts/freestanding-c.md) — why `-ffreestanding -nostdlib`
- [../Home.md](../Home.md) — wiki home
