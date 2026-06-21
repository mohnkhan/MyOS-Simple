[← Home](../Home.md)

# BIOS Services

*The firmware's built-in routines — video, disk, and keyboard — that MyOS-Simple calls through software interrupts while it still lives in real mode.*

Before an operating system can talk to hardware directly, it has to *be* an
operating system. The boot sector starts with nothing: no drivers, no display
code, no disk routines. The BIOS solves this chicken-and-egg problem by exposing
a library of ready-made hardware routines, callable through **software
interrupts**, that work the moment your code starts running in
[real mode](real-mode.md).

MyOS-Simple leans on three of them: `int 0x10` (video), `int 0x13` (disk), and
`int 0x16` (keyboard).

## How a BIOS call works

The mechanism is the [Interrupt Vector Table](../reference/memory-map.md) at
physical `0x00000`: 256 entries, each a far pointer to a handler. When you
execute `int N`, the CPU looks up entry `N` and calls the routine it points to.
The BIOS fills these entries during [POST](boot-process.md), so by the time our
code runs, `int 0x10`, `int 0x13`, and `int 0x16` all resolve to firmware
routines.

You select *which* service within an interrupt by loading a **function number**
into `AH`, then pass arguments in the other registers, and finally execute the
`int`. The convention is simple but unforgiving — every register matters.

```asm
    mov ah, 0x00    ; function: set video mode
    mov al, 0x03    ; argument: mode 3 (80x25 text)
    int 0x10        ; invoke the BIOS video service
```

`helloworld-os-asm/main.asm:15-17`

> 💡 **Tidbit:** BIOS interrupt numbers are *not* the same as CPU exception
> numbers. `int 0x10`, `0x13`, `0x16` are firmware conventions — entries in the
> IVT that the BIOS chose to populate. In [protected mode](protected-mode.md)
> those same vectors get reused for the kernel's own
> [interrupt descriptor table](../reference/glossary.md), which is one reason the
> BIOS services stop working there.

> ⚠️ **Caveat:** BIOS services are **16-bit real-mode only**. The instant the
> bootloader sets `CR0.PE` and enters [protected mode](protected-mode.md), all
> of `int 0x10/0x13/0x16` become unusable. Everything in this article must
> happen *before* that switch. After it, the kernel drives hardware directly —
> e.g. writing the [VGA text buffer](vga-text-mode.md) at `0xB8000` and reading
> the [PS/2 keyboard](ps2-keyboard-8042.md) through I/O ports.

## `int 0x10` — Video services

The video BIOS is the busiest service in MyOS-Simple. It is used by both
[stage 1](../stages/stage-1-assembly-boot.md) builds and by the
[bootloader](boot-process.md) to clear the screen before the mode switch.

### `AH = 0x00` — Set video mode

```asm
    mov ah, 0x00
    mov al, 0x03    ; AL = mode number
    int 0x10
```

`AL = 3` selects the standard **80×25, 16-colour text mode**. Calling this also
clears the screen as a side effect, so MyOS-Simple uses it as a one-instruction
"reset the display" at the start of the boot sector
(`helloworld-os-c/boot.asm:29-31`).

### `AH = 0x02` — Set cursor position

```asm
    mov ah, 0x02
    mov bh, 0       ; BH = display page (0)
    mov dh, 12      ; DH = row    (0-24)
    mov dl, 34      ; DL = column (0-79)
    int 0x10
```

`helloworld-os-asm/main.asm:20-24`

Stage 1 uses this to *centre* its messages: row 12, column 34 for
`"Hello, World!"`, and row 14, column 31 for the signature line. The colour
build uses it both to place each label and, inside its print loop, to advance the
cursor one column after each character (`main_color.asm:172-174`).

### `AH = 0x06` — Scroll / clear a window

```asm
    mov ah, 0x06
    mov al, 0          ; AL = lines to scroll (0 = clear whole window)
    mov bh, WHITE_ON_BLUE  ; BH = attribute for blanked cells
    mov cx, 0x0000     ; CX = top-left  (row 0,  col 0)
    mov dx, 0x184F     ; DX = bottom-right (row 24, col 79)
    int 0x10
```

`helloworld-os-asm/main_color.asm:28-33`

This is how the colour build paints its blue background: scrolling a window by
zero lines with `AL = 0` blanks the whole region, filling every cell with the
attribute in `BH`. The corner `0x184F` decodes as row `0x18 = 24`, column `0x4F
= 79` — the bottom-right of an 80×25 screen.

> 💡 **Tidbit:** `DX = 0x184F` packs two coordinates into one 16-bit register:
> the high byte (`DH = 0x18 = 24`) is the row, the low byte (`DL = 0x4F = 79`)
> is the column. Many BIOS calls pack row/column into `DH`/`DL` this way, which
> is why the bottom-right corner of a text screen is the memorable constant
> `0x184F`.

### `AH = 0x09` — Write character + attribute

```asm
    mov ah, 0x09
    mov al, <char>     ; AL = character to write
    mov bh, 0          ; BH = page
    mov bl, <attr>     ; BL = attribute (colour)
    mov cx, 1          ; CX = repeat count
    int 0x10
```

`helloworld-os-asm/main_color.asm:166-169`

Unlike teletype output, `AH = 0x09` writes a character *with a colour
attribute* but does **not** move the cursor — that is why the colour build
follows each write with an `AH = 0x02` cursor advance. The attribute byte is
`(background << 4) | foreground`; see [VGA text mode](vga-text-mode.md) for the
full colour table.

### `AH = 0x0E` — Teletype output

```asm
print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_string
.done:
    ret
```

`helloworld-os-asm/main.asm:55-63`

This is the workhorse of the monochrome build. `AH = 0x0E` prints the character
in `AL` *and advances the cursor*, handling line wrap and scrolling for you —
behaving like a simple terminal ("teletype"). Paired with `lodsb`, it makes a
classic null-terminated string printer.

> 💡 **Tidbit:** `lodsb` ("load string byte") loads the byte at `DS:SI` into
> `AL` and increments `SI` in one instruction. Followed by `or al, al / jz`
> (test for the null terminator) it is the beating heart of nearly every
> real-mode print loop — including both of MyOS-Simple's stage 1 builds and the
> bootloader's `print_16` error routine (`helloworld-os-c/boot.asm:93-104`).

## `int 0x13` — Disk services

From [stage 2](../stages/stage-2-c-protected-mode.md) onward, the boot sector is
too small to hold the kernel, so it reads the kernel off disk with `int 0x13`,
function `AH = 0x02` (read sectors):

```asm
    mov ah, 0x02        ; BIOS read sectors
    mov al, dh          ; Number of sectors
    mov cl, 0x02        ; Start from sector 2
    mov ch, 0x00        ; Cylinder 0
    mov dh, 0x00        ; Head 0
    int 0x13
    jc disk_error
```

`helloworld-os-c/boot.asm:70-77`

The destination is `ES:BX`, the geometry is **CHS** (cylinder/head/sector), and
the carry flag signals an error. This service is involved enough to have its own
page: [disk loading with INT 13h](disk-loading-int13.md).

## `int 0x16` — Keyboard services

Both stage 1 builds wait for the user with `int 0x16`, function `AH = 0x00`
(blocking read):

```asm
.wait_key:
    mov ah, 0x00
    int 0x16
    cmp al, 'q'     ; lowercase q
    je shutdown
    cmp al, 'Q'     ; uppercase Q
    je shutdown
    jmp .wait_key
```

`helloworld-os-asm/main.asm:40-47`

`AH = 0x00` **blocks** until a key is pressed, then returns the BIOS
**scancode** in `AH` and the **ASCII** value in `AL`. The monochrome build only
inspects `AL` (looking for `q`/`Q` to quit); the colour build compares `AL`
against `'1'`–`'5'` and space to recolour the screen
(`helloworld-os-asm/main_color.asm:63-101`).

> ⚠️ **Caveat:** `int 0x16` is the *easy* way to read the keyboard, but it only
> works in real mode. Once MyOS-Simple reaches its interactive
> [shell](../stages/stage-3-interactive-shell.md) in protected mode, it must
> talk to the [8042 keyboard controller](ps2-keyboard-8042.md) directly through
> I/O ports and decode raw [scancodes](scancodes.md) itself — there is no
> firmware to do it anymore.

## Quick reference

| Interrupt | `AH` | Service | Key registers |
|-----------|------|---------|---------------|
| `0x10` | `0x00` | Set video mode | `AL` = mode (`3` = 80×25 text) |
| `0x10` | `0x02` | Set cursor position | `DH` = row, `DL` = col, `BH` = page |
| `0x10` | `0x06` | Scroll / clear window | `AL` = lines, `BH` = attr, `CX` = TL, `DX` = BR |
| `0x10` | `0x09` | Write char + attribute | `AL` = char, `BL` = attr, `CX` = count |
| `0x10` | `0x0E` | Teletype output | `AL` = char (advances cursor) |
| `0x13` | `0x02` | Read disk sectors | `AL` = count, `CH/CL/DH` = CHS, `ES:BX` = dest |
| `0x16` | `0x00` | Blocking key read | returns `AH` = scancode, `AL` = ASCII |

A fuller list of I/O port and BIOS details is collected in the
[I/O ports reference](../reference/io-ports.md).

## See also

- [Real mode](real-mode.md) — the only environment where these services exist
- [The boot process](boot-process.md) — where each call fits in the boot timeline
- [Disk loading with INT 13h](disk-loading-int13.md) — the full `int 0x13` story
- [VGA text mode](vga-text-mode.md) — the attribute byte and direct `0xB8000` access
- [PS/2 keyboard (8042)](ps2-keyboard-8042.md) — reading the keyboard after the BIOS is gone
- [Scancodes](scancodes.md) — decoding raw key codes in protected mode
- [Stage 1: assembly boot](../stages/stage-1-assembly-boot.md) — the builds shown above
- [I/O ports reference](../reference/io-ports.md)
- [Home](../Home.md)
