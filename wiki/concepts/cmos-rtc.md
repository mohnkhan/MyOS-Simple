# CMOS RTC

*Reading wall-clock time and date from the battery-backed real-time clock through I/O ports 0x70 and 0x71.*

The **CMOS RTC** (Real-Time Clock) is the chip that knows what time it is even when the
machine is powered off. MyOS-Simple talks to it directly — no BIOS calls, no libc — to drive
the `clock`, `date`, and `uptime` shell commands introduced in
[Stage 4](../stages/stage-4-clock-processes-calc.md). This article walks through the register
map, the BCD-vs-binary conversion, the update-in-progress dance, and the simplified uptime
arithmetic, all as implemented in `rtc.h` and `rtc.c`.

> 💡 **Tidbit:** The CMOS RTC chip descends from the Motorola **MC146818**, which has kept PC
> time since the IBM PC/AT in 1984. It lives on a sliver of low-power CMOS SRAM kept alive by
> the little coin cell on your motherboard, which is why your clock and BIOS settings survive a
> power-off — and why a dead battery makes the date reset to something like 1980 on every boot.

## The two-port interface

The RTC is not memory-mapped; it is reached through a pair of [I/O ports](../reference/io-ports.md).
You never read a time register directly. Instead you **select** a register by writing its number
to the address port, then **read or write** its value through the data port:

```c
// CMOS/RTC I/O ports
#define CMOS_ADDRESS    0x70
#define CMOS_DATA       0x71
```

The two access primitives are tiny (`rtc.c:33`):

```c
// Read a register from CMOS
uint8_t rtc_read_register(uint8_t reg) {
    outb(CMOS_ADDRESS, reg);   // select register `reg`
    return inb(CMOS_DATA);     // read its current value
}

// Write a register to CMOS
void rtc_write_register(uint8_t reg, uint8_t value) {
    outb(CMOS_ADDRESS, reg);   // select register `reg`
    outb(CMOS_DATA, value);    // write the value
}
```

`inb`/`outb` are the usual inline-assembly wrappers around the x86 `in`/`out` instructions
(`rtc.c:17`). Because reading a value is always a *select-then-read* pair, the address port acts
as a stateful cursor into CMOS — set it once, and the data port refers to that register until you
move the cursor again.

## The register map

CMOS exposes one byte per time field. MyOS uses these addresses (`rtc.h:18`):

| Register | Address | Meaning |
| --- | --- | --- |
| `RTC_SECONDS` | `0x00` | Seconds (0–59) |
| `RTC_MINUTES` | `0x02` | Minutes (0–59) |
| `RTC_HOURS` | `0x04` | Hours (0–23 or 1–12 + PM bit) |
| `RTC_WEEKDAY` | `0x06` | Day of week (1–7) |
| `RTC_DAY` | `0x07` | Day of month (1–31) |
| `RTC_MONTH` | `0x08` | Month (1–12) |
| `RTC_YEAR` | `0x09` | Year, last two digits |
| `RTC_CENTURY` | `0x32` | Century (if present) |
| `RTC_STATUS_A` | `0x0A` | Status A (update-in-progress flag) |
| `RTC_STATUS_B` | `0x0B` | Status B (format flags) |

Note the gaps: registers `0x01`, `0x03`, and `0x05` are the *alarm* registers, which MyOS does
not use, so the time fields land on even addresses.

## Status registers: format flags and the UIP bit

Two control registers govern how the values are encoded and whether the clock is mid-tick.

**Status B** holds the format flags (`rtc.h:30`):

```c
#define RTC_24HOUR      0x02    // 24-hour mode flag
#define RTC_BINARY      0x04    // Binary mode flag (vs BCD)
```

`rtc_init()` reads Status B and forces 24-hour mode on if it is not already set, so the rest of
the code can assume hours are 0–23 (`rtc.c:51`):

```c
void rtc_init(void) {
    uint8_t status_b = rtc_read_register(RTC_STATUS_B);
    if (!(status_b & RTC_24HOUR)) {
        rtc_write_register(RTC_STATUS_B, status_b | RTC_24HOUR);
    }
}
```

**Status A** carries the **update-in-progress (UIP)** flag in bit 7 (`0x80`). The RTC ticks once
per second, and during that tick the time registers are momentarily inconsistent. Before reading,
MyOS spins until the flag clears (`rtc.c:45`):

```c
static void rtc_wait_update(void) {
    // Wait for any update in progress to finish
    while (rtc_read_register(RTC_STATUS_A) & 0x80);
}
```

> ⚠️ **Caveat:** This guards the *start* of the read but not its *end*. If a CMOS update begins
> after `rtc_wait_update()` returns but before the seven registers have all been read, you can
> capture a torn value — for example reading `10:59:59` and then, one register later,
> `11:00:00`'s minute. The canonical fix used by production kernels is to **read the whole set
> twice and compare**, retrying until two consecutive reads agree, or to re-check UIP after the
> read. MyOS keeps the simpler single-pass version; for a clock display sampled once a second the
> odds of a tear are tiny, but they are not zero.

## BCD vs binary

By default the RTC stores each field as **binary-coded decimal (BCD)**: each decimal digit gets
its own 4-bit nibble. So 59 seconds reads back as `0x59`, *not* `0x3B` (which is decimal 59 in
plain binary). BCD is friendly for humans and seven-segment displays but useless for arithmetic,
so MyOS converts (`rtc.c:28`):

```c
uint8_t bcd_to_binary(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}
```

The high nibble is the tens digit (multiply by 10), the low nibble is the ones digit. The
conversion only runs when the `RTC_BINARY` bit is **clear** — if a particular RTC is already in
binary mode, the raw values are used as-is (`rtc.c:80`):

```c
status_b = rtc_read_register(RTC_STATUS_B);
if (!(status_b & RTC_BINARY)) {
    time->second = bcd_to_binary(time->second);
    time->minute = bcd_to_binary(time->minute);
    time->hour   = bcd_to_binary(time->hour);
    /* ... day, month, year, weekday ... */
}
```

> 💡 **Tidbit:** BCD wastes bits — a byte can hold 0–255 in binary but only 0–99 in two-nibble
> BCD — yet it sidesteps decimal-to-binary conversion entirely on hardware that just wants to
> *show* the number. The same trade-off (decimal exactness over binary range) shows up in
> MyOS's calculator; see [fixed-point arithmetic](fixed-point-arithmetic.md).

## Reading a full timestamp

`rtc_read_time()` ties it together: wait for the update window, read all seven registers,
convert from BCD if needed, normalize the hour, and compute the full year (`rtc.c:61`):

```c
void rtc_read_time(rtc_time_t* time) {
    uint8_t status_b;
    uint8_t century = 20;  // Default to 21st century

    rtc_wait_update();

    time->second  = rtc_read_register(RTC_SECONDS);
    time->minute  = rtc_read_register(RTC_MINUTES);
    time->hour    = rtc_read_register(RTC_HOURS);
    time->day     = rtc_read_register(RTC_DAY);
    time->month   = rtc_read_register(RTC_MONTH);
    time->year    = rtc_read_register(RTC_YEAR);
    time->weekday = rtc_read_register(RTC_WEEKDAY);
    /* ...BCD conversion... */
    /* ...12-hour normalization... */
    time->year += (century * 100);   // 25 -> 2025
}
```

### The 12-hour PM bit

Even though `rtc_init()` requests 24-hour mode, the read path still handles 12-hour clocks
defensively. In 12-hour mode the top bit of the hour register (`0x80`) is the **PM flag**
(`rtc.c:92`):

```c
if (!(status_b & RTC_24HOUR)) {
    uint8_t pm = time->hour & 0x80;   // PM flag in high bit
    time->hour &= 0x7F;               // strip the flag
    if (pm && time->hour != 12) {
        time->hour += 12;             // 1 PM..11 PM -> 13..23
    } else if (!pm && time->hour == 12) {
        time->hour = 0;               // 12 AM -> 00
    }
}
```

### The year and the century

CMOS stores only the last two digits of the year. MyOS reconstructs the full year by adding a
**hardcoded** century of 20, giving `25 → 2025` (`rtc.c:63`, `rtc.c:104`). The century register
at `0x32` exists in the map but is not consulted, so the clock will read 21st-century dates
regardless of what the hardware reports — fine for a tutorial, a bug for a clock meant to outlive
the year 2099.

## Formatting

Two helpers turn the `rtc_time_t` struct into display strings, building characters by hand since
there is no `sprintf`:

- `rtc_get_time_string()` → `HH:MM:SS` (`rtc.c:133`)
- `rtc_get_date_string()` → `DD/MM/YYYY` (`rtc.c:159`)

Both lean on `num_to_str_padded()`, which zero-pads single-digit values to two characters so that
9 o'clock shows as `09`, not `9`.

## Uptime

Uptime is derived, not counted. At startup, `rtc_record_boot_time()` snapshots the current time
once into a static `boot_time` (`rtc.c:220`). Thereafter `rtc_get_uptime_seconds()` reads the
clock again and returns the difference (`rtc.c:296`), and `rtc_get_uptime_string()` formats it as
`[N days, ]HH:MM:SS` (`rtc.c:308`).

The difference engine, `calculate_time_diff()`, handles three cases (`rtc.c:248`):

1. **Same day** — subtract seconds-since-midnight; if the end is *earlier*, the clock crossed
   midnight, so the result is `(86400 - start) + end`.
2. **Same month, later day** — multiply whole days by 86400 and adjust by the start/end offsets.
3. **Different month or year** — a deliberately simplified fallback.

The leap-year test inside `days_in_month()` is correct (`rtc.c:236`):

```c
if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
    return 29;   // February in a leap year
}
```

> ⚠️ **Caveat:** The third uptime branch is explicitly a shortcut. The comments in
> `calculate_time_diff()` say as much — *"For simplicity, assume maximum 30 days uptime"* — and
> the code sets `days = 1` then returns an approximation rather than walking the calendar across
> month and year boundaries (`rtc.c:268`). For a hobby OS that rarely runs for weeks this never
> surfaces, but an uptime spanning two months will be wrong. The same-day and single-month paths
> are accurate.

## How the shell uses it

The `clock`, `date`, and `uptime` commands are thin wrappers: they call the `rtc_get_*_string`
helpers and print the result. See the [command reference](../reference/command-reference.md) for
the user-facing syntax and the [Stage 4 walkthrough](../stages/stage-4-clock-processes-calc.md)
for how RTC support was added alongside the process model and the calculator.

## See also

- [I/O ports reference](../reference/io-ports.md) — the full 0x70/0x71 port map and `in`/`out` usage
- [Fixed-point arithmetic](fixed-point-arithmetic.md) — the same decimal-over-binary trade-off, in the calculator
- [Cooperative scheduling](cooperative-scheduling.md) — the other half of Stage 4
- [Stage 4: clock, processes, calculator](../stages/stage-4-clock-processes-calc.md)
- [Command reference](../reference/command-reference.md) — `clock`, `date`, `uptime`
- [Glossary](../reference/glossary.md) — BCD, RTC, UIP
- [Home](../Home.md)
