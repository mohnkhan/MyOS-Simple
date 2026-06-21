# VGA Text Mode

*How a freestanding kernel paints characters on screen by writing bytes straight into memory — no driver, no syscall, no BIOS.*

When the BIOS hands control to your boot sector, the display is already in **VGA text mode 3**: an 80-column by 25-row grid of character cells, 16 colors. The screen is not something you "draw" pixel by pixel — instead, a region of physical memory *is* the screen. Write a byte there and a glyph appears. This is the simplest possible output device, and it is why MyOS-Simple can print "Hello, World!" from a kernel that contains no libc, no `printf`, and no graphics code at all.

## The memory-mapped framebuffer

The text-mode framebuffer lives at **physical address `0xB8000`**. It is *memory-mapped*, not an I/O port — you reach it with an ordinary pointer dereference, not with `inb`/`outb`. (Contrast this with the [PS/2 keyboard](ps2-keyboard-8042.md), which *is* read through I/O ports.)

```c
#define VIDEO_MEMORY 0xb8000
```
— `kernel.c:12`, and identically `shell.c:13`.

Because the project runs in flat 32-bit [protected mode](protected-mode.md) with an identity-mapped [GDT](global-descriptor-table.md), the linear address `0xB8000` maps directly to that physical address, so the pointer "just works."

## Cell layout: two bytes per character

Each of the 80×25 = 2000 cells is **two bytes**:

| Byte | Meaning |
|------|---------|
| 0 (even) | The ASCII character code |
| 1 (odd)  | The **attribute** (color) byte |

So the whole screen is `80 * 25 * 2 = 4000` bytes. The offset of cell `(x, y)` is:

```c
int offset = (y * 80 + x) * 2;
video[offset]     = c;     // character
video[offset + 1] = attr;  // color
```
— `putchar_at`, `kernel.c:51-56` (and `shell.c:44-49`).

> 💡 **Tidbit:** The `(y * 80 + x) * 2` formula is the text-mode equivalent of a *stride* calculation in a pixel framebuffer — `80` is the row stride in cells and `2` is the bytes-per-cell. Every framebuffer you ever touch, graphical or textual, computes addresses this same way.

The pointer is declared `volatile` so the compiler never caches or reorders the writes — every store must actually reach video memory:

```c
volatile char* video = (volatile char*)VIDEO_MEMORY;
```
— `kernel.c:52`.

## The attribute byte

The attribute byte packs a background color in the high nibble and a foreground color in the low nibble:

```
  bit:  7   6 5 4   3 2 1 0
       [B] [ BG  ] [  FG   ]
```

`make_color` builds one by hand:

```c
char make_color(char bg, char fg) {
    return (bg << 4) | fg;
}
```
— `kernel.c:46-48`. Equivalently, `attribute = (background << 4) | foreground`.

### The 16 colors

The low nibble (foreground) can be any of 16 colors `0x0`–`0xF`:

| Code | Color | Code | Color |
|------|-------|------|-------|
| `0x0` | BLACK | `0x8` | DARK_GRAY |
| `0x1` | BLUE | `0x9` | LIGHT_BLUE |
| `0x2` | GREEN | `0xA` | LIGHT_GREEN |
| `0x3` | CYAN | `0xB` | LIGHT_CYAN |
| `0x4` | RED | `0xC` | LIGHT_RED |
| `0x5` | MAGENTA | `0xD` | LIGHT_MAGENTA |
| `0x6` | BROWN | `0xE` | YELLOW |
| `0x7` | LIGHT_GRAY | `0xF` | WHITE |

These are defined as macros in `kernel.c:15-30`. A combined constant like `WHITE_ON_BLACK` is simply `0x0F` (`kernel.c:33`) — black background nibble `0`, white foreground nibble `F`.

The kernel even renders a live swatch of all 16 by drawing space characters whose *background* equals their *foreground*:

```c
for (int i = 0; i < 16; i++) {
    putchar_at(' ', 13 + i*2, 23, make_color(i, i));
    putchar_at(' ', 13 + i*2 + 1, 23, make_color(i, i));
}
```
— `kernel.c:293-296`.

> ⚠️ **Caveat:** In standard VGA text mode, **bit 7 of the attribute byte** (the top bit of the *background* nibble) is the **blink** attribute by default, not an extra background-color bit. That is why the background is effectively limited to **8** colors (`0x0`–`0x7`) while the foreground gets all 16: setting a "background" of `0x8`–`0xF` instead makes the cell blink. Blink can be disabled via a VGA register to free that bit for backgrounds, but MyOS-Simple never touches it, so high background codes will blink.

## Clearing the screen

There is no "clear" instruction — you blank the screen by writing a space into every cell with a chosen attribute:

```c
void clear_screen_color(char bg_color) {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    char attr = make_color(bg_color, WHITE);
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video[i] = ' ';
        video[i + 1] = attr;
    }
}
```
— `kernel.c:73-80`. The shell's `clear_screen` is the same loop with a fixed `WHITE_ON_BLACK` attribute (`shell.c:51-59`), and it also resets the software cursor to `(0, 0)`.

## Scrolling

VGA text mode has no scroll hardware that the project uses, so the shell scrolls by hand: when the cursor passes the bottom row it copies rows 1–24 up into rows 0–23, then blanks the last row.

```c
void scroll_screen() {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    // Move all lines up by one
    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 80; x++) {
            int src_offset = ((y + 1) * 80 + x) * 2;
            int dst_offset = (y * 80 + x) * 2;
            video[dst_offset]     = video[src_offset];
            video[dst_offset + 1] = video[src_offset + 1];
        }
    }
    // Clear the last line
    for (int x = 0; x < 80; x++) {
        int offset = (24 * 80 + x) * 2;
        video[offset]     = ' ';
        video[offset + 1] = WHITE_ON_BLACK;
    }
}
```
— `shell.c:61-80`. It is triggered from `putchar` when `cursor_y >= 25` (`shell.c:101-104`).

## The cursor is software-only

There is no hardware text cursor management in this project. The shell tracks a software cursor with two integers (`cursor_x`, `cursor_y` — `shell.c:33-34`) and advances them inside `putchar` (`shell.c:82-105`). The color demo draws a literal underscore glyph to *look* like a cursor and erases it by overwriting with a space:

```c
// Show cursor
putchar_at('_', cursor_x, cursor_y, WHITE_ON_BLACK);
...
// Clear cursor
putchar_at(' ', cursor_x, cursor_y, WHITE_ON_BLACK);
```
— `kernel.c:304` and `kernel.c:311`. The real blinking hardware cursor (controlled via CRTC registers on ports `0x3D4`/`0x3D5`) is never programmed.

> 💡 **Tidbit:** Because output is just memory writes, the kernel can update *any* cell at *any* time without "moving" a cursor first — the live keyboard-modifier status line (`show_keyboard_status`, `kernel.c:205-250`) and the cursor underscore are both drawn directly at fixed coordinates while the input loop runs.

## See also

- [PS/2 Keyboard & the 8042 Controller](ps2-keyboard-8042.md) — the input counterpart, read via I/O ports rather than memory
- [Protected Mode](protected-mode.md) — the 32-bit flat environment that makes `0xB8000` directly addressable
- [Global Descriptor Table](global-descriptor-table.md) — the identity-mapped segments behind that flat address space
- [Freestanding C](freestanding-c.md) — why there is no `printf` and you write bytes by hand
- [Stage 2: C in Protected Mode](../stages/stage-2-c-protected-mode.md) — where the color demo lives
- [Stage 3: Interactive Shell](../stages/stage-3-interactive-shell.md) — where scrolling and the software cursor are used
- [Memory Map](../reference/memory-map.md) — where `0xB8000` sits relative to the kernel and stack
- [I/O Ports Reference](../reference/io-ports.md) — the ports VGA text mode does *not* use for the framebuffer (and the CRTC ports it could)
- [Home](../Home.md)
