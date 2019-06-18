# Stage 1 — Hello World OS in Assembly (16-bit real mode)

A minimal operating system that boots directly from a disk image, prints a
message using BIOS services, and waits for a keypress to halt. It runs in
16-bit real mode with nothing beneath it but the firmware. Two variants are
provided: a monochrome teletype version and a colorful, interactive version.

This is the foundation of the series. Everything above it
(`helloworld-os-c` and later) replaces these BIOS calls with code that talks to
the hardware directly. Start here.


## What it does

**`main.asm` (monochrome, 60 lines).** Clears the screen, positions the cursor,
prints `Hello, World!` and `Booted with love ;)` centered on the display, then
waits for `q`/`Q` to halt the CPU.

**`main_color.asm` (color + interactive, 183 lines).** Paints a blue background,
draws a title, message, signature, and a help line in distinct colors, then
enters a keyboard loop:

- Keys `1`–`5` change the background color.
- `SPACE` resets to the default blue background.
- `Q`/`q` clears the screen, prints a goodbye message, and halts.

Each keypress redraws the text so the foreground survives a background change.


## How it works

The BIOS, on power-up, reads the first sector (512 bytes) of the boot device
into physical memory at `0x7C00` and jumps there in 16-bit real mode. For the
BIOS to accept a sector as bootable, its final two bytes must be the signature
`0x55 0xAA` (little-endian `0xAA55`).

Both programs are therefore organized as a single boot sector:

```
[BITS 16]              ; 16-bit real mode
[ORG 0x7C00]           ; tell the assembler where the code will live
...                    ; program body
times 510-($-$$) db 0  ; pad to 510 bytes
dw 0xAA55              ; boot signature in bytes 511-512
```

There is no kernel and no second stage. The entire OS is the boot sector.

### BIOS services used

| Interrupt | Function | Purpose |
|-----------|----------|---------|
| `int 0x10`, `AH=0x00` | Set video mode | Mode `0x03` = 80×25 16-color text |
| `int 0x10`, `AH=0x02` | Set cursor position | Place text at a given row/column |
| `int 0x10`, `AH=0x06` | Scroll/clear window | Fill the screen with a background attribute |
| `int 0x10`, `AH=0x09` | Write char + attribute | Print a character in a specific color |
| `int 0x10`, `AH=0x0E` | Teletype output | Print a character at the cursor (monochrome version) |
| `int 0x16`, `AH=0x00` | Wait for keystroke | Blocking read of the next key |

### Color model

A VGA text attribute byte is `(background << 4) | foreground`, each nibble a
4-bit color index. For example `0x1F` is white text on a blue background and
`0x0E` is yellow on black. The color version computes new background attributes
by shifting the pressed digit into the high nibble.

### Halting

After the goodbye path the CPU is stopped with `cli` (mask interrupts) followed
by `hlt`, then an unconditional jump back to `hlt` to guarantee it never resumes.


## Source layout

```
helloworld-os-asm/
├── main.asm              Monochrome Hello World boot sector.
├── main_color.asm        Color + interactive boot sector.
├── Makefile              Build and run targets.
├── helloworld.img        Build output (monochrome, 512 bytes).
└── helloworld_color.img  Build output (color, 512 bytes).
```


## Prerequisites

```sh
sudo apt install nasm qemu-system-x86      # Debian/Ubuntu
```

No compiler or linker is needed — these are flat binaries assembled directly by
NASM.


## Build

```sh
make            # assemble both images
make rebuild    # clean, then assemble both
```

The build invokes, for each variant:

```sh
nasm -f bin main.asm       -o helloworld.img
nasm -f bin main_color.asm -o helloworld_color.img
```

`-f bin` emits a flat binary with no object-file headers, which is exactly what
a boot sector must be. Because of the `times 510-($-$$) db 0` padding, each
output is exactly 512 bytes.


## Run

```sh
make run         # boot the monochrome version
make run-color   # boot the color version
```

Both targets launch:

```sh
qemu-system-x86_64 -drive format=raw,file=<image>
```

`format=raw` tells QEMU to treat the file as a raw disk rather than a container
format, so the BIOS sees the boot sector at LBA 0.

Controls (color version): `1`–`5` set the background, `SPACE` resets, `Q` quits.


## Verifying the image

The image must be exactly 512 bytes and end in `55 AA`:

```sh
stat -c%s helloworld.img            # -> 512
xxd -s 510 -l 2 helloworld.img      # -> 0000fe: 55aa
```


## Notes and limitations

- Real mode gives direct access to BIOS interrupts but only ~1 MiB of address
  space and 16-bit registers. This is why later stages switch to protected mode.
- There is no input echo or line editing; `int 0x16` reads a single key.
- The program never returns to the BIOS — it halts the machine. Close QEMU to
  exit.

Next stage: `helloworld-os-c` replaces the BIOS print routines with a C kernel
running in 32-bit protected mode.
