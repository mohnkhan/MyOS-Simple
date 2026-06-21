# I/O Ports

*The hardware port numbers MyOS-Simple reads and writes — keyboard, CMOS — plus the real-mode BIOS interrupts it leaves behind.*

The x86 has a second address space, separate from memory, reached with the `in` and
`out` instructions. MyOS-Simple uses exactly four ports: two for the
[8042 keyboard controller](../concepts/ps2-keyboard-8042.md) and two for the
[CMOS real-time clock](../concepts/cmos-rtc.md). Everything else it talks to — the
display — is *memory*-mapped, not port-mapped.

## Port map

| Port | Direction | Meaning | Used by |
|------|-----------|---------|---------|
| `0x60` | R / W | Keyboard data: read a [scancode](../concepts/scancodes.md), or write a command/byte to the device | [ps2-keyboard](../concepts/ps2-keyboard-8042.md) |
| `0x64` | R = status, W = command | Read the 8042 status register; write a controller command | [ps2-keyboard](../concepts/ps2-keyboard-8042.md) |
| `0x70` | W | CMOS register select (bit 7 also controls NMI disable) | [rtc](../concepts/cmos-rtc.md) |
| `0x71` | R / W | CMOS data: read/write the register selected via `0x70` | [rtc](../concepts/cmos-rtc.md) |

Defined as `KEYBOARD_DATA_PORT 0x60` / `KEYBOARD_STATUS_PORT 0x64`
([`keyboard.h:15-16`](../concepts/ps2-keyboard-8042.md)) and `CMOS_ADDRESS 0x70` /
`CMOS_DATA 0x71` ([`rtc.h:14-15`](../concepts/cmos-rtc.md)).

### 8042 status register bits (read from `0x64`)

| Bit | Mask | Name | Meaning when set |
|-----|------|------|------------------|
| 0 | `0x01` | Output buffer full | A byte (scancode) is waiting in `0x60` — safe to read |
| 1 | `0x02` | Input buffer full | The controller has not consumed your last write to `0x60`/`0x64` — wait before writing |

`KEYBOARD_OUTPUT_FULL 0x01` and `KEYBOARD_INPUT_FULL 0x02` in `keyboard.h:19-20`.
Polling waits for bit 0 before reading a scancode — this is [polling, not
interrupts](glossary.md#polling-vs-interrupts).

## The CMOS register file (reached through `0x70`/`0x71`)

You select a register by writing its index to `0x70`, then read or write the value
through `0x71`. The RTC fields MyOS-Simple uses ([`rtc.h:18-31`](../concepts/cmos-rtc.md)):

| Register | Index | Field |
|----------|-------|-------|
| `RTC_SECONDS` | `0x00` | Seconds |
| `RTC_MINUTES` | `0x02` | Minutes |
| `RTC_HOURS` | `0x04` | Hours |
| `RTC_WEEKDAY` | `0x06` | Day of week |
| `RTC_DAY` | `0x07` | Day of month |
| `RTC_MONTH` | `0x08` | Month |
| `RTC_YEAR` | `0x09` | Year |
| `RTC_CENTURY` | `0x32` | Century (if present) |
| `RTC_STATUS_A` | `0x0A` | Status A (contains the UIP flag) |
| `RTC_STATUS_B` | `0x0B` | Status B (24-hour / binary-vs-[BCD](glossary.md#bcd) flags) |

Status B flags: `RTC_24HOUR 0x02`, `RTC_BINARY 0x04`.

## VGA framebuffer is memory-mapped, not a port

The display is **not** reached with `in`/`out`. The VGA text framebuffer lives at the
physical address `0xB8000` ([MMIO](glossary.md#mmio)); the kernel writes characters and
[attribute bytes](../concepts/vga-text-mode.md) directly into that memory. See the
[memory map](memory-map.md) and [VGA text mode](../concepts/vga-text-mode.md).

## Real-mode BIOS interrupts (not ports)

Before the switch to [protected mode](../concepts/protected-mode.md), the bootloader
uses [BIOS services](../concepts/bios-services.md) via software interrupts. These are
**only available in real mode** — once `CR0.PE` is set they are gone, which is exactly
why the C stages talk to ports directly.

| Interrupt | Service | Used for |
|-----------|---------|----------|
| `int 0x10` | Video | Clear screen, teletype print in the [bootloader](../concepts/boot-sector.md) |
| `int 0x13` | Disk | [Read the kernel sectors](../concepts/disk-loading-int13.md) (CHS) from `0x1000` |
| `int 0x16` | Keyboard | Stage 1 keyboard input |

## Accessing ports from C

The C stages wrap `in`/`out` in inline-assembly helpers
([`rtc.c:17-25`](../concepts/cmos-rtc.md)):

```c
static inline unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(unsigned short port, unsigned char data) {
    __asm__ __volatile__("outb %0, %1" : : "a"(data), "Nd"(port));
}
```

| Constraint | Role |
|------------|------|
| `"=a"(result)` | Output: the read byte comes back in `AL` |
| `"a"(data)` | Input: the byte to write goes in `AL` |
| `"Nd"(port)` | Port number: an 8-bit immediate (`N`) or `DX` (`d`) |

> 💡 **Tidbit:** The `"Nd"` constraint exists because the `in`/`out` opcodes can take
> the port either as an 8-bit immediate (ports 0–255) or in the `DX` register. Ports
> `0x60`–`0x71` fit in a byte, so the assembler can encode them inline; larger port
> numbers would force the `DX` form.

> ⚠️ **Caveat:** Reading `0x60` without first checking status bit 0 of `0x64` returns
> stale or garbage data. The polling loop *must* spin on output-buffer-full before
> consuming a scancode, or keystrokes will be dropped and duplicated.

> 💡 **Tidbit:** Bit 7 of the value written to `0x70` is the NMI-disable bit. Writing
> a bare register index (0x00–0x7F) leaves NMIs enabled; some firmware code is careful
> to OR in `0x80` while touching CMOS. MyOS-Simple writes plain indices.

## See also

- [PS/2 keyboard & the 8042](../concepts/ps2-keyboard-8042.md) — ports `0x60`/`0x64`
- [Scancodes](../concepts/scancodes.md) and [scancode tables](scancode-tables.md)
- [CMOS / RTC](../concepts/cmos-rtc.md) — ports `0x70`/`0x71`
- [BIOS services](../concepts/bios-services.md) — the real-mode interrupts
- [Memory map](memory-map.md) — the MMIO framebuffer at `0xB8000`
- [Glossary](glossary.md)
- [Home](../Home.md)
