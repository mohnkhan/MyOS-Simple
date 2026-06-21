[← Home](../Home.md)

# PS/2 Keyboard & the 8042 Controller

*Reading keystrokes without the BIOS — busy-waiting on the 8042 controller through two I/O ports.*

In [real mode](real-mode.md), reading the keyboard is a one-liner: call BIOS interrupt `int 0x16` and it hands you a key. But the moment MyOS-Simple switches to [protected mode](protected-mode.md), the BIOS interrupt vector table is gone and `int 0x16` is unavailable. From here on, the kernel must talk to the keyboard hardware itself — the **8042 PS/2 controller** — by reading and writing I/O ports.

## The 8042: two ports, one keyboard

The keyboard controller exposes itself through two I/O ports. Unlike the [VGA framebuffer](vga-text-mode.md) (which is memory-mapped at `0xB8000`), these are true **port-mapped I/O**, reached with the `inb`/`outb` instructions:

| Port | Name | Direction | Purpose |
|------|------|-----------|---------|
| `0x60` | data port | read/write | the scancode byte to read |
| `0x64` | status / command port | read = status, write = command | tells you whether data is ready |

```c
#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64
```
— `keyboard.h:15-16`.

The project only ever *reads* these ports — it never sends commands to reconfigure the controller — so `0x64` is used purely as a status register here.

## The status byte

Reading port `0x64` returns a status byte. MyOS-Simple cares about its two lowest bits:

```c
#define KEYBOARD_OUTPUT_FULL 0x01
#define KEYBOARD_INPUT_FULL  0x02
```
— `keyboard.h:19-20`.

- **Bit 0 (`0x01`), output buffer full** — set when the controller has a byte waiting for *you* in port `0x60`. This is the bit you poll before reading.
- **Bit 1 (`0x02`), input buffer full** — set when *you* have written a byte the controller has not yet consumed. You would check this before writing a command.

> ⚠️ **Caveat:** The names are written from the *controller's* point of view, which feels backwards. "Output full" means the controller's output (your input) is ready to read. "Input full" means the controller's input (your command bytes) is still pending. Read them as "is there a key for me?" and "has the controller eaten my command yet?"

## inb: reading a port from C

There is no standard library, so the `inb` helper is a tiny piece of inline assembly wrapping the x86 `inb` instruction:

```c
unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}
```
— `shell.c:38-42` (identical in `kernel.c:112-116`). The `"=a"` constraint puts the result in `AL`; `"Nd"` lets the port number be either an 8-bit immediate or the `DX` register.

## The polling read loop

To read a key, the kernel **busy-waits** until the output buffer is full, then reads the data port. This is the heart of all keyboard input in the project:

```c
// Wait for keyboard data
while(!(inb(KEYBOARD_STATUS_PORT) & KEYBOARD_OUTPUT_FULL));

// Read scancode
scancode = inb(KEYBOARD_DATA_PORT);
```
— `shell.c:145-148` (and `kernel.c:124-127`).

The `while` loop spins, re-reading `0x64`, until bit 0 turns on; only then is it safe to read `0x60`. Reading the data port also clears the output-full bit, so the next iteration will wait again until the next keystroke arrives.

> 💡 **Tidbit:** This pattern is called **polling** or *busy-waiting*. The CPU does nothing useful while spinning on `inb(0x64)` — it just burns cycles checking the same bit over and over. It is the simplest possible way to wait for hardware and perfectly fine for a single-tasking demo OS.

> ⚠️ **Caveat:** Polling is **not** how a real OS reads the keyboard. A production kernel programs the [PIC](protected-mode.md) to deliver hardware interrupt **IRQ1** whenever a key is pressed, then services it in an interrupt handler registered in the IDT. MyOS-Simple has **no IDT, no PIC setup, and no IRQ handling at all** — it relies entirely on the polling loop above. That keeps the code short but means the CPU can never sleep or do background work while waiting for input.

## From byte to keystroke

The byte you read from port `0x60` is a **scancode**, not an ASCII character. A press produces a "make code"; a release produces a "break code" (the make code with bit `0x80` set). The kernel decodes the release bit, tracks modifier keys, and looks the make code up in a translation table — all of which is covered in [Scancodes & Translation](scancodes.md):

```c
// Handle key release
if (scancode & KEY_RELEASE_BIT) {
    scancode &= ~KEY_RELEASE_BIT;
    ...
    return 0;
}
```
— `shell.c:151-159`.

## Two readers, two levels of detail

The project contains two keyboard readers built on the same polling loop:

- **`get_key_advanced`** in `kernel.c:119-193` — full handling: shift, ctrl, alt, caps lock, arrow keys, and function keys, with a `KeyboardState` struct tracking modifier state.
- **`get_key`** in `shell.c:140-181` — a deliberately simpler version: shift only, plus Enter and Backspace, suited to line-editing at the `shell>` prompt.

Both share `KeyboardState`:

```c
typedef struct {
    unsigned char shift_pressed;
    unsigned char ctrl_pressed;
    unsigned char alt_pressed;
    unsigned char caps_lock;
} KeyboardState;
```
— `keyboard.h:23-28`.

## See also

- [Scancodes & Translation](scancodes.md) — decoding the byte that `inb(0x60)` returns
- [VGA Text Mode](vga-text-mode.md) — the output side, which uses memory-mapped I/O instead of ports
- [Real Mode](real-mode.md) — where `int 0x16` was still available
- [Protected Mode](protected-mode.md) — why the BIOS keyboard service disappears
- [Stage 3: Interactive Shell](../stages/stage-3-interactive-shell.md) — the shell that polls the keyboard for its prompt
- [I/O Ports Reference](../reference/io-ports.md) — the full list of ports the project touches
- [Home](../Home.md)
