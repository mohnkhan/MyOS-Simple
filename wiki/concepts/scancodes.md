# Scancodes & Translation

*Turning a raw keyboard byte into a character: make codes, break codes, the `0x80` release bit, and two lookup tables.*

The byte you read from the keyboard's [8042 controller](ps2-keyboard-8042.md) at port `0x60` is a **scancode** — a number that identifies a physical key and whether it was pressed or released. It is *not* ASCII. The letter `q` is not byte `0x71` ('q'); it is scancode `0x10`. Turning that number into the character `'q'` (or `'Q'`, depending on modifiers) is the job described here.

## Scancode Set 1

The keyboard sends **Set 1** scancodes — the original IBM XT keyboard set. Each key has a fixed make code; the kernel's translation tables are indexed directly by that code.

> 💡 **Tidbit:** Set 1 is ancient — it dates to the 1981 IBM PC — yet it is still what bare-metal code sees today. Modern USB keyboards send entirely different codes, but the controller (or BIOS, in legacy/translation mode) converts them *back* to Set 1 for compatibility. Writing to Set 1 means your decoder works on hardware from 1981 to 2026.

## Make codes and break codes

Every key press generates a **make code**. Releasing the same key generates a **break code**, which is just the make code with the high bit (`0x80`) set:

```
make code   q  = 0x10   (0001 0000)
break code  q  = 0x90   (1001 0000)   <- bit 7 set
```

So the decoder tests bit `0x80` to tell press from release, then masks it off to recover the original key:

```c
#define KEY_RELEASE_BIT 0x80
...
// Handle key release
if (scancode & KEY_RELEASE_BIT) {
    scancode &= ~KEY_RELEASE_BIT;
    ...
    return 0; // Return 0 for key releases
}
```
— `keyboard.h:90` and `kernel.c:130-142`.

Both readers return `0` on a release so the input loop ignores it — except that releasing a modifier key (shift/ctrl/alt) first clears that modifier's flag in `KeyboardState` (`kernel.c:133-140`).

## The translation tables

Two static tables in `keyboard.h` map a make code to an ASCII character — one for the unshifted key, one for the shifted key:

```c
static const char scancode_to_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',  // 0-9
    '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r',  // 10-19
    ...
};
static const char scancode_to_ascii_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*',  // 0-9
    '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R',  // 20-29
    ...
};
```
— `keyboard.h:32-55`.

The index *is* the make code. Scancode `0x10` (decimal 16) lands on `'q'` / `'Q'`; scancode `0x02` lands on `'1'` / `'!'`. Entries that have no printable character (modifier keys, unused codes) are `0`. The full byte-by-byte tables are in the [scancode tables reference](../reference/scancode-tables.md).

> ⚠️ **Caveat:** The tables only cover indices `0`–`89`, which is why both readers guard the lookup with `if (scancode < 90)` (`kernel.c:161`, `shell.c:168`). A scancode past the end of the table would read out of bounds — but Set 1's printable keys all fall within this range, and special keys are handled separately by name.

## Choosing which table

The decoder picks the shifted table when shift (or, in the full kernel, caps lock) is active:

```c
if (scancode < 90) {
    if (kbd_state.shift_pressed || kbd_state.caps_lock) {
        ascii_char = scancode_to_ascii_shift[scancode];
    } else {
        ascii_char = scancode_to_ascii[scancode];
    }
    // Handle caps lock for letters
    if (kbd_state.caps_lock && !kbd_state.shift_pressed) {
        if (ascii_char >= 'a' && ascii_char <= 'z') ascii_char -= 32;
    } else if (kbd_state.caps_lock && kbd_state.shift_pressed) {
        if (ascii_char >= 'A' && ascii_char <= 'Z') ascii_char += 32;
    }
}
```
— `kernel.c:161-178`.

The extra `caps_lock` logic fixes a subtlety: caps lock should affect *letters only*, but the shifted table also turns `1`→`!`, `2`→`@`, and so on. So when caps lock alone is on, the code selects the shifted table for uppercase letters, then any non-letter that got shifted is left as-is by the range checks; and when caps lock *and* shift are both held, letters are flipped back to lowercase (`+= 32`), matching how a real keyboard behaves.

> 💡 **Tidbit:** The `-= 32` / `+= 32` trick exploits the ASCII layout: uppercase and lowercase letters are exactly 32 apart (`'A'` = 65, `'a'` = 97). Subtracting 32 uppercases; adding 32 lowercases. No table needed.

The shell's simpler `get_key` skips caps entirely — shift only (`shell.c:168-173`).

## Modifier scancodes

Modifier keys are recognized by their make codes and update `KeyboardState` instead of producing a character:

| Modifier | Scancode | Macro |
|----------|----------|-------|
| Left Shift | `0x2A` | `SCANCODE_LEFT_SHIFT` |
| Right Shift | `0x36` | `SCANCODE_RIGHT_SHIFT` |
| Left Ctrl | `0x1D` | `SCANCODE_LEFT_CTRL` |
| Left Alt | `0x38` | `SCANCODE_LEFT_ALT` |
| Caps Lock | `0x3A` | `SCANCODE_CAPS_LOCK` |

— `keyboard.h:58-62`. Shift/ctrl/alt set their flag on the make code and clear it on the break code; caps lock *toggles* on each press (`kernel.c:155-157`):

```c
} else if (scancode == SCANCODE_CAPS_LOCK) {
    kbd_state.caps_lock = !kbd_state.caps_lock;
    return 0;
}
```

## Special keys

Non-character keys are handled by name after the table lookup. The full kernel maps arrows and function keys to sentinel return values, plus the universal Enter/Backspace/Tab/Esc:

```c
if (scancode == SCANCODE_UP)    return 'U';
if (scancode == SCANCODE_DOWN)  return 'D';
if (scancode == SCANCODE_LEFT)  return 'L';
if (scancode == SCANCODE_RIGHT) return 'R';
if (scancode == SCANCODE_F1)    return '1';
...
if (scancode == SCANCODE_ESC)       return 27;
if (scancode == SCANCODE_ENTER)     return '\n';
if (scancode == SCANCODE_BACKSPACE) return '\b';
if (scancode == SCANCODE_TAB)       return '\t';
```
— `kernel.c:181-190`. Their codes (`keyboard.h:63-87`): ESC `0x01`, ENTER `0x1C`, BACKSPACE `0x0E`, TAB `0x0F`, SPACE `0x39`, F1–F10 `0x3B`–`0x44`, UP `0x48`, LEFT `0x4B`, RIGHT `0x4D`, DOWN `0x50`, HOME `0x47`, END `0x4F`, PAGE_UP `0x49`, PAGE_DOWN `0x51`, INSERT `0x52`, DELETE `0x53`.

> ⚠️ **Caveat:** On real hardware, the extended keys (arrows, Home/End, etc.) are sent as a **two-byte sequence prefixed with `0xE0`** — e.g. Up is `0xE0 0x48`, not bare `0x48`. MyOS-Simple does **not** handle the `0xE0` prefix; it reads a single byte and matches `0x48` directly. This works under QEMU, which delivers the simpler single-byte codes, but the arrow keys may misbehave on physical hardware that sends the full extended sequence.

## See also

- [PS/2 Keyboard & the 8042 Controller](ps2-keyboard-8042.md) — how the scancode byte arrives at port `0x60`
- [VGA Text Mode](vga-text-mode.md) — where the decoded character is finally drawn
- [Scancode Tables](../reference/scancode-tables.md) — the complete Set 1 byte tables, normal and shifted
- [Stage 2: C in Protected Mode](../stages/stage-2-c-protected-mode.md) — the full modifier/arrow decoder (`get_key_advanced`)
- [Stage 3: Interactive Shell](../stages/stage-3-interactive-shell.md) — the simpler shift-only `get_key`
- [Home](../Home.md)
