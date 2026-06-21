# Stage 1 — The Assembly Boot Sector

*The entire operating system is 512 bytes, including the two-byte signature the firmware looks for.*

Stage 1 strips a computer down to the very first thing it runs. There is no
kernel, no second stage, no C, and no runtime of any kind — just one 512-byte
sector that the BIOS loads to memory and executes in 16-bit real mode. It clears
the screen, prints a message using BIOS services, waits for a keypress, and
halts. That is the whole program, and it is a complete, bootable operating
system.

Two variants ship in this stage: a minimal monochrome version and a larger
colorful, interactive version.

- Directory: `helloworld-os-asm/`
- Mode: 16-bit real mode
- Language: pure NASM assembly
- Files: `main.asm` (~69 lines), `main_color.asm` (~193 lines), `Makefile`


## What's new

This is the first stage, so everything is new. The concepts it introduces are
the foundation every later stage builds on:

- The **boot-sector contract**: the firmware loads exactly 512 bytes from the
  first sector of the boot media to physical address `0x7C00`, checks that the
  last two bytes are the signature `0xAA55`, and jumps in.
- **16-bit real mode**, the CPU state the machine powers up in — see
  [real-mode.md](../concepts/real-mode.md).
- The **BIOS services** that are the only API available before you write your
  own: video (`int 0x10`) and keyboard (`int 0x16`). See
  [bios-services.md](../concepts/bios-services.md).
- The **VGA attribute byte** (color variant), encoding background and foreground
  into a single byte.

For the wider picture of how power-on leads to your code running, read
[boot-process.md](../concepts/boot-process.md) and
[boot-sector.md](../concepts/boot-sector.md).


## The files

| File | Role |
|------|------|
| `main.asm` | The minimal monochrome OS: clear screen, print two centered strings, wait for `q`/`Q`, halt. |
| `main_color.asm` | The interactive color OS: blue background, colored text, keys 1–5 change the background, SPACE resets, `Q` quits. |
| `Makefile` | Assembles both into raw `.img` images and runs them in QEMU. |

Each `.asm` file is assembled directly into a flat binary image with `nasm -f
bin`. There is no linker and no separate boot sector — the file *is* the boot
sector.


## Code walkthrough: the monochrome boot sector

The whole of `main.asm` is worth reading top to bottom; it is short enough to
hold in your head.

### Telling the assembler where the code lives

```asm
[BITS 16]
[ORG 0x7C00]
```

`[BITS 16]` tells NASM to emit 16-bit instructions (the machine is in real
mode). `[ORG 0x7C00]` tells it that the code will run at address `0x7C00`, so
every absolute reference to a label (`msg`, `sign`) is computed relative to that
base. This is the address the BIOS always loads the boot sector to.

### Clearing the screen and positioning the cursor

```asm
start:
    ; clear screen
    mov ah, 0x00
    mov al, 0x03
    int 0x10
```

`int 0x10` is the BIOS video service. With `AH = 0x00` it sets the video mode;
mode `0x03` is the standard 80×25 16-color text mode. Setting the mode also
clears the screen. The cursor is then placed with the same interrupt, `AH =
0x02`:

```asm
    mov ah, 0x02
    mov bh, 0          ; page 0
    mov dh, 12         ; row
    mov dl, 34         ; column
    int 0x10
```

### Printing with teletype output

The print routine is a classic `lodsb` loop over a null-terminated string:

```asm
print_string:
    lodsb              ; AL = [SI], SI++
    or al, al          ; AL == 0 ?
    jz .done
    mov ah, 0x0E       ; teletype output
    int 0x10
    jmp print_string
.done:
    ret
```

`lodsb` loads the byte at `DS:SI` into `AL` and advances `SI`. `or al, al` sets
the zero flag if `AL` is zero, ending the loop at the string terminator.
Otherwise `int 0x10` with `AH = 0x0E` (teletype) prints the character and
advances the cursor automatically. (`main.asm:55`)

### Waiting for a key, then halting

```asm
.wait_key:
    mov ah, 0x00
    int 0x16           ; BIOS: wait for keystroke -> AL = ASCII
    cmp al, 'q'
    je shutdown
    cmp al, 'Q'
    je shutdown
    jmp .wait_key

shutdown:
    cli                ; disable interrupts
    hlt                ; halt the CPU
    jmp $              ; safety net if an NMI wakes it
```

`int 0x16` with `AH = 0x00` blocks until a key is pressed and returns its ASCII
code in `AL`. On `q`/`Q` the CPU clears interrupts and halts; `jmp $` is an
infinite self-loop in case a non-maskable interrupt ever resumes execution.

### The signature

```asm
times 510 - ($ - $$) db 0
dw 0xAA55
```

`$` is the current address and `$$` the start of the section, so `$ - $$` is the
number of bytes emitted so far. `times 510 - ($ - $$) db 0` pads with zeros up
to byte 510, and `dw 0xAA55` writes the two-byte boot signature into bytes 510–511.
Without that signature in exactly those bytes the BIOS refuses to boot the disk.

> 💡 **Tidbit:** The signature is stored little-endian, so on disk the bytes are
> `0x55 0xAA`, even though the source writes `dw 0xAA55`. The firmware checks for
> precisely this pair at offsets 510 and 511 of the first sector.


## Code walkthrough: the color variant

`main_color.asm` keeps the same skeleton but adds attribute-driven color and an
interactive loop. The key new mechanism is the **VGA attribute byte**, computed
as `(background << 4) | foreground`:

```asm
%define WHITE_ON_BLUE   0x1F     ; bg=1 (blue), fg=F (white)
%define YELLOW_ON_BLACK 0x0E     ; bg=0 (black), fg=E (yellow)
```

It paints a full-screen blue window by scrolling a rectangle with `int 0x10`,
`AH = 0x06` (scroll up; with `AL = 0` it clears the window to the attribute in
`BH`):

```asm
    mov ah, 0x06
    mov al, 0          ; AL=0 clears the whole window
    mov bh, WHITE_ON_BLUE
    mov cx, 0x0000     ; top-left = (0,0)
    mov dx, 0x184F     ; bottom-right = (24,79)
    int 0x10
```

Colored text is drawn with `AH = 0x09` (write character *and* attribute at the
cursor) followed by `AH = 0x02` to advance the cursor one column, because
`AH = 0x09` does not move the cursor itself:

```asm
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x09       ; write char + attribute
    mov bh, 0
    mov cx, 1          ; repeat count
    int 0x10
    inc dl             ; next column
    mov ah, 0x02       ; reposition cursor
    int 0x10
    jmp .loop
```

The keyboard loop reads a key with `int 0x16` and reacts:

```asm
    cmp al, '1'
    jb keyboard_loop
    cmp al, '5'
    ja check_special
    sub al, '0'        ; '1'..'5' -> 1..5
    shl al, 4          ; move into the background nibble
    or al, 0x0F        ; keep a white foreground
    mov bh, al
    ; ... int 0x10 AH=06 to repaint, then redraw the text
```

Keys `1`–`5` select a background color (the digit becomes the high nibble of the
attribute), SPACE resets to the default blue, and `Q` clears the screen, prints
"Goodbye!", and halts. (`main_color.asm:63`)

> 💡 **Tidbit:** Because `AH = 0x09` writes to the framebuffer but leaves the
> cursor where it was, the color routine has to issue a separate `AH = 0x02`
> after every character. The simpler `AH = 0x0E` teletype call used in the
> monochrome version advances the cursor for free — but can't set a color.


## How to build and run

From `helloworld-os-asm/`:

```sh
make            # assemble both helloworld.img and helloworld_color.img
make run        # boot the monochrome image in QEMU
make run-color  # boot the color/keyboard image in QEMU
make clean      # remove the .img files
make help       # list targets
```

The build is just two NASM invocations — no compiler, no linker:

```sh
nasm -f bin main.asm       -o helloworld.img
nasm -f bin main_color.asm -o helloworld_color.img
```

For the toolchain and how the images are produced across all stages, see
[toolchain-and-build.md](../reference/toolchain-and-build.md) and
[building-and-running.md](../guides/building-and-running.md).


## What it teaches

- The minimal contract a machine demands before it will run your code: 512
  bytes, loaded at `0x7C00`, ending in `0xAA55`.
- Real mode and the segmented 16-bit world the CPU boots into.
- Using BIOS interrupts as the only available system services.
- VGA text attributes and how a single byte encodes both colors.
- Why a "Hello, World" with no OS underneath it is a meaningfully different
  problem from one written against a C library.


## Known limits

- **No second stage and no kernel.** Everything must fit in 512 bytes, which is
  why the program is so small.
- **BIOS-only.** Every service used here (`int 0x10`, `int 0x16`) exists only in
  real mode. The moment Stage 2 switches to protected mode, all of it
  disappears and the kernel must talk to hardware directly.
- **16-bit, 1 MiB addressable, no protection.** Real mode offers no memory
  protection and a 20-bit address space.
- **Halting is final.** `cli; hlt` stops the machine; there is no shutdown or
  reboot path beyond resetting the emulator.


## Next stage

The hardest conceptual leap in the whole tutorial comes next: not learning C,
but getting the machine into a state where C can run at all — installing a GDT,
flipping the protection-enable bit, far-jumping into 32-bit code, and linking a
freestanding kernel to a fixed load address.

→ [Stage 2 — A C Kernel in Protected Mode](stage-2-c-protected-mode.md)


## See also

- [boot-process.md](../concepts/boot-process.md) — power-on to first instruction
- [boot-sector.md](../concepts/boot-sector.md) — the 512-byte / `0xAA55` contract
- [real-mode.md](../concepts/real-mode.md) — 16-bit real mode in detail
- [bios-services.md](../concepts/bios-services.md) — `int 0x10` / `int 0x16`
- [vga-text-mode.md](../concepts/vga-text-mode.md) — text cells and attribute bytes
- [memory-map.md](../reference/memory-map.md) — where things live in memory
- [toolchain-and-build.md](../reference/toolchain-and-build.md) — assembling the image
- [Home](../Home.md)
