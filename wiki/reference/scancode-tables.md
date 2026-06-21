# Scancode Tables

*The complete Set-1 scancode-to-ASCII translation tables and named scancodes, reproduced exactly from `keyboard.h`.*

When the [8042 controller](../concepts/ps2-keyboard-8042.md) delivers a byte on port
`0x60`, it is a **Set-1 [scancode](../concepts/scancodes.md)**, not a character. The C
stages translate it through two lookup tables in
[`keyboard.h`](../concepts/ps2-keyboard-8042.md): one for keys without `Shift`, one for
shifted keys. This page reproduces both tables verbatim, plus the named modifier and
function-key scancodes.

A pressed key sends a **make** code; releasing it sends a **break** code, which is the
make code with bit 7 set (`+ 0x80`). The `KEY_RELEASE_BIT 0x80` constant marks releases,
which the shell ignores.

## `scancode_to_ascii[]` — unshifted (`keyboard.h:32-42`)

| Scancode (dec) | Hex | Key | ASCII produced |
|----------------|-----|-----|----------------|
| 0 | `0x00` | (none) | 0 |
| 1 | `0x01` | Esc | 27 (`ESC`) |
| 2 | `0x02` | 1 | `1` |
| 3 | `0x03` | 2 | `2` |
| 4 | `0x04` | 3 | `3` |
| 5 | `0x05` | 4 | `4` |
| 6 | `0x06` | 5 | `5` |
| 7 | `0x07` | 6 | `6` |
| 8 | `0x08` | 7 | `7` |
| 9 | `0x09` | 8 | `8` |
| 10 | `0x0A` | 9 | `9` |
| 11 | `0x0B` | 0 | `0` |
| 12 | `0x0C` | - | `-` |
| 13 | `0x0D` | = | `=` |
| 14 | `0x0E` | Backspace | `\b` |
| 15 | `0x0F` | Tab | `\t` |
| 16 | `0x10` | Q | `q` |
| 17 | `0x11` | W | `w` |
| 18 | `0x12` | E | `e` |
| 19 | `0x13` | R | `r` |
| 20 | `0x14` | T | `t` |
| 21 | `0x15` | Y | `y` |
| 22 | `0x16` | U | `u` |
| 23 | `0x17` | I | `i` |
| 24 | `0x18` | O | `o` |
| 25 | `0x19` | P | `p` |
| 26 | `0x1A` | [ | `[` |
| 27 | `0x1B` | ] | `]` |
| 28 | `0x1C` | Enter | `\n` |
| 29 | `0x1D` | Left Ctrl | 0 (modifier) |
| 30 | `0x1E` | A | `a` |
| 31 | `0x1F` | S | `s` |
| 32 | `0x20` | D | `d` |
| 33 | `0x21` | F | `f` |
| 34 | `0x22` | G | `g` |
| 35 | `0x23` | H | `h` |
| 36 | `0x24` | J | `j` |
| 37 | `0x25` | K | `k` |
| 38 | `0x26` | L | `l` |
| 39 | `0x27` | ; | `;` |
| 40 | `0x28` | ' | `'` |
| 41 | `0x29` | ` | `` ` `` |
| 42 | `0x2A` | Left Shift | 0 (modifier) |
| 43 | `0x2B` | \ | `\` |
| 44 | `0x2C` | Z | `z` |
| 45 | `0x2D` | X | `x` |
| 46 | `0x2E` | C | `c` |
| 47 | `0x2F` | V | `v` |
| 48 | `0x30` | B | `b` |
| 49 | `0x31` | N | `n` |
| 50 | `0x32` | M | `m` |
| 51 | `0x33` | , | `,` |
| 52 | `0x34` | . | `.` |
| 53 | `0x35` | / | `/` |
| 54 | `0x36` | Right Shift | 0 (modifier) |
| 55 | `0x37` | Keypad * | `*` |
| 56 | `0x38` | Left Alt | 0 (modifier) |
| 57 | `0x39` | Space | ` ` (space) |
| 74 | `0x4A` | Keypad - | `-` |
| 78 | `0x4E` | Keypad + | `+` |

All indices not listed (58–73, 75–77, 79–89) map to `0`.

## `scancode_to_ascii_shift[]` — shifted (`keyboard.h:45-55`)

Only the entries that *differ* from the unshifted table are shown; everything else is
identical (including `Esc`, Backspace `\b`, Tab `\t`, Enter `\n`, Space, and the keypad
`*` `-` `+`).

| Scancode (dec) | Hex | Key | Unshifted | Shifted |
|----------------|-----|-----|-----------|---------|
| 2 | `0x02` | 1 | `1` | `!` |
| 3 | `0x03` | 2 | `2` | `@` |
| 4 | `0x04` | 3 | `3` | `#` |
| 5 | `0x05` | 4 | `4` | `$` |
| 6 | `0x06` | 5 | `5` | `%` |
| 7 | `0x07` | 6 | `6` | `^` |
| 8 | `0x08` | 7 | `7` | `&` |
| 9 | `0x09` | 8 | `8` | `*` |
| 10 | `0x0A` | 9 | `9` | `(` |
| 11 | `0x0B` | 0 | `0` | `)` |
| 12 | `0x0C` | - | `-` | `_` |
| 13 | `0x0D` | = | `=` | `+` |
| 16–25 | `0x10`–`0x19` | Q…P | `qwertyuiop` | `QWERTYUIOP` |
| 26 | `0x1A` | [ | `[` | `{` |
| 27 | `0x1B` | ] | `]` | `}` |
| 30–38 | `0x1E`–`0x26` | A…L | `asdfghjkl` | `ASDFGHJKL` |
| 39 | `0x27` | ; | `;` | `:` |
| 40 | `0x28` | ' | `'` | `"` |
| 41 | `0x29` | ` | `` ` `` | `~` |
| 43 | `0x2B` | \ | `\` | `|` |
| 44–50 | `0x2C`–`0x32` | Z…M | `zxcvbnm` | `ZXCVBNM` |
| 51 | `0x33` | , | `,` | `<` |
| 52 | `0x34` | . | `.` | `>` |
| 53 | `0x35` | / | `/` | `?` |

## Named scancodes (`keyboard.h:58-90`)

### Modifiers and editing

| Constant | Hex | Key |
|----------|-----|-----|
| `SCANCODE_ESC` | `0x01` | Esc |
| `SCANCODE_BACKSPACE` | `0x0E` | Backspace |
| `SCANCODE_TAB` | `0x0F` | Tab |
| `SCANCODE_LEFT_CTRL` | `0x1D` | Left Ctrl |
| `SCANCODE_ENTER` | `0x1C` | Enter |
| `SCANCODE_LEFT_SHIFT` | `0x2A` | Left Shift |
| `SCANCODE_RIGHT_SHIFT` | `0x36` | Right Shift |
| `SCANCODE_LEFT_ALT` | `0x38` | Left Alt |
| `SCANCODE_SPACE` | `0x39` | Space |
| `SCANCODE_CAPS_LOCK` | `0x3A` | Caps Lock |

### Function keys

| Constant | Hex | Constant | Hex |
|----------|-----|----------|-----|
| `SCANCODE_F1` | `0x3B` | `SCANCODE_F6` | `0x40` |
| `SCANCODE_F2` | `0x3C` | `SCANCODE_F7` | `0x41` |
| `SCANCODE_F3` | `0x3D` | `SCANCODE_F8` | `0x42` |
| `SCANCODE_F4` | `0x3E` | `SCANCODE_F9` | `0x43` |
| `SCANCODE_F5` | `0x3F` | `SCANCODE_F10` | `0x44` |

### Navigation (keypad / extended)

| Constant | Hex | Key |
|----------|-----|-----|
| `SCANCODE_HOME` | `0x47` | Home |
| `SCANCODE_UP` | `0x48` | Up arrow |
| `SCANCODE_PAGE_UP` | `0x49` | Page Up |
| `SCANCODE_LEFT` | `0x4B` | Left arrow |
| `SCANCODE_RIGHT` | `0x4D` | Right arrow |
| `SCANCODE_END` | `0x4F` | End |
| `SCANCODE_DOWN` | `0x50` | Down arrow |
| `SCANCODE_PAGE_DOWN` | `0x51` | Page Down |
| `SCANCODE_INSERT` | `0x52` | Insert |
| `SCANCODE_DELETE` | `0x53` | Delete |

### The release bit

| Constant | Hex | Meaning |
|----------|-----|---------|
| `KEY_RELEASE_BIT` | `0x80` | Set on **break** codes; a release is `make_code \| 0x80` |

## Worked examples

| Event | Byte on `0x60` | Decode |
|-------|----------------|--------|
| Press `A` (no shift) | `0x1E` | `scancode_to_ascii[0x1E]` → `'a'` |
| Press `A` with Shift held | `0x2A` then `0x1E` | shift flag set, then `scancode_to_ascii_shift[0x1E]` → `'A'` |
| Release `A` | `0x9E` | `0x9E & 0x80` ≠ 0 → release, ignored |
| Release Left Shift | `0xAA` | `0x2A \| 0x80` → clears shift flag |

> 💡 **Tidbit:** Set 1 is the original IBM PC/XT scancode set. PS/2 keyboards default
> to Set 2, but the 8042 controller *translates* Set 2 back to Set 1 by default — which
> is why a Set-1 table works against a modern PS/2 (or emulated) keyboard with no
> reconfiguration.

> ⚠️ **Caveat:** The extended navigation keys (arrows, Home, Delete, …) actually arrive
> as a *two-byte* sequence prefixed with `0xE0`. The single-byte constants here name the
> second byte; code that distinguishes, say, keypad `7` from `Home` must watch for the
> `0xE0` prefix. The shell treats them as ordinary single bytes.

> 💡 **Tidbit:** A break code is just the make code with bit 7 set, so the maximum make
> code in Set 1 is `0x7F`. That is the whole reason `KEY_RELEASE_BIT` is `0x80`: it is
> the first bit no make code can ever use.

## See also

- [Scancodes](../concepts/scancodes.md) — make/break and Set 1 explained
- [PS/2 keyboard & the 8042](../concepts/ps2-keyboard-8042.md) — reading port `0x60`
- [I/O ports](io-ports.md) — `0x60`/`0x64` and the status bits
- [Stage 3: interactive shell](../stages/stage-3-interactive-shell.md) — where these tables drive input
- [Glossary](glossary.md)
- [Home](../Home.md)
