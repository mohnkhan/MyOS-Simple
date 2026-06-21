# Stage 4 — Clock, Processes, and a Calculator

*What does an operating system actually do? Tell the time, run tasks, compute — and make the shell pleasant to use.*

Stage 3 gave the kernel a shell with five commands. Stage 4 answers a bigger
question — what an OS is actually *for* — by adding three real subsystems and a
much richer shell. It reads wall-clock time from the CMOS real-time clock, models
processes with a control block and a cooperative scheduler, evaluates arithmetic
with fixed-point numbers (no FPU math library), and makes the prompt ergonomic
with command history, Tab completion, and aliases. The command count grows from
5 to **20**.

- Directory: `helloworld-os-c-v2/`
- Mode: 32-bit protected mode
- Language: C (multiple modules) + NASM
- Built kernel: `shell.c` (~1,322 lines)


## What's new vs Stage 3

| | Stage 3 | Stage 4 |
|---|---|---|
| Modules | one `shell.c` | `shell.c` + **`rtc.c`** + **`process.c`** |
| Commands | 5 | **20** |
| Clock | none | CMOS RTC: `time`, `date`, `clock`, `uptime` |
| Processes | none | PCB + cooperative scheduler: `ps`, `run`, `kill`, `suspend`, `resume` |
| Math | none | fixed-point `calc` (`+ - * /`, 3 decimals) |
| Line editing | backspace + shift | + **history** (↑/↓, `history`), **Tab completion**, **aliases** |
| Sectors loaded | 15 | **39** (`boot.asm:35`) |

The boot path is unchanged from Stage 2/3 (GDT, protected mode, kernel at
`0x1000`, stack at `0x90000`). The bootloader simply reads more sectors because
the kernel is now roughly 19.5 KB across three compiled objects.

> ⚠️ **Caveat:** `kernel.c` is present in this directory but is **not compiled**.
> The Makefile builds `shell.o`, `process.o`, and `rtc.o` — `kernel.c` is a
> leftover reference copy of the Stage 2 demo. When reading this stage, `shell.c`
> is the kernel.


## The files

| File | Role |
|------|------|
| `shell.c` | The kernel and shell: VGA, keyboard, history/completion/aliases, the calculator, and the 20-command dispatcher. |
| `process.c` / `process.h` | The process control block and cooperative round-robin scheduler model. |
| `rtc.c` / `rtc.h` | The CMOS real-time clock and uptime tracking. |
| `keyboard.h` | Scancode tables (as in earlier stages). |
| `kernel.h`, `stdint.h`, `stddef.h` | Small freestanding headers (fixed-width types, `NULL`, kernel print helpers). |
| `README_SHELL.md` | The complete shell command reference for this stage. |
| `kernel.c` | **Unused** reference copy — not built. |


## Code walkthrough: the CMOS real-time clock

`rtc.c` reads time and date from the CMOS chip through two I/O ports: `0x70`
selects a register, `0x71` reads or writes its value.

```c
uint8_t rtc_read_register(uint8_t reg) {
    outb(CMOS_ADDRESS, reg);    // 0x70: select the register
    return inb(CMOS_DATA);      // 0x71: read its value
}
```

CMOS stores each field (seconds, minutes, hours, day, month, year, weekday) in
**binary-coded decimal** by default — each nibble is one decimal digit. The
conversion is a one-liner:

```c
uint8_t bcd_to_binary(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}
```

A full read waits out any update in progress, reads every register, then converts
from BCD (and from 12-hour to 24-hour form if needed):

```c
void rtc_read_time(rtc_time_t* time) {
    rtc_wait_update();                       // spin while Status A bit 7 is set
    time->second  = rtc_read_register(RTC_SECONDS);
    time->minute  = rtc_read_register(RTC_MINUTES);
    time->hour    = rtc_read_register(RTC_HOURS);
    /* ... day, month, year, weekday ... */
    uint8_t status_b = rtc_read_register(RTC_STATUS_B);
    if (!(status_b & RTC_BINARY)) {          // values are BCD -> convert
        time->second = bcd_to_binary(time->second);
        /* ... and the rest ... */
    }
    time->year += (century * 100);           // 2-digit year -> 4-digit
}
```

`rtc_record_boot_time()` is called once at startup, and `uptime` is the
difference between "now" and that recorded timestamp (`calculate_time_diff`
handles the same-day case and a midnight wrap). The register map, the BCD
encoding, and the update-in-progress flag are explained in
[cmos-rtc.md](../concepts/cmos-rtc.md); the ports are listed in
[io-ports.md](../reference/io-ports.md).

> ⚠️ **Caveat:** `rtc_wait_update()` waits for the update-in-progress flag to
> *clear* before reading, but the reads themselves are not re-checked against a
> second update. If a CMOS update begins midway through the seven register reads,
> a field can momentarily be inconsistent. A fully robust reader loops until two
> consecutive reads agree; this one keeps it simple.


## Code walkthrough: the process model

`process.c` introduces a **Process Control Block** — `process_t` in `process.h`
— holding a PID, name, state, a saved CPU-context struct, a statically allocated
4 KiB stack, a priority, and a `next` pointer for the ready queue:

```c
typedef struct process_t {
    uint32_t pid;
    char name[PROCESS_NAME_LEN];
    process_state_t state;          // READY / RUNNING / BLOCKED / TERMINATED
    cpu_context_t context;          // saved registers
    uint8_t* stack;
    uint32_t stack_size;
    void (*entry_point)(void);
    uint32_t time_slice;
    uint32_t priority;
    struct process_t* next;
} process_t;
```

`process_create` finds a free table slot, allocates one of ten static 4 KiB
stacks, initializes the PCB and its context (entry point, stack pointer,
`eflags = 0x200`), and links it onto the ready queue. The scheduler is plain
round-robin: it rotates the current process to the tail of the queue and picks
the new head.

The crucial honesty of this stage is here:

```c
void process_scheduler(void) {
    /* ... rotate ready_queue, pick next ... */
    if (next && next != current_process) {
        process_t* old = current_process;
        current_process = next;
        current_process->state = PROCESS_RUNNING;

        // Context switch would happen here in real implementation
        // context_switch(&old->context, &current->context);
    }
}
```

The scheduler updates *bookkeeping* — which PCB is "current", queue order,
states — but the actual register-level context switch is **not performed**. The
`extern context_switch()` declared in `process.h` is never implemented and never
called; the line that would invoke it is a comment (`process.c:237`). Scheduling
is also **cooperative**: a task only relinquishes the CPU when it calls
`process_yield()` (which just calls the scheduler). There is no timer
interrupt and therefore no preemption.

The three sample workloads — `process_counter`, `process_fibonacci`,
`process_prime` — each loop, busy-wait, and yield:

```c
void process_counter(void) {
    int count = 0;
    while (1) {
        print_string("[Counter] Count: ");
        print_int(count++);
        print_string("\n");
        for (volatile int i = 0; i < 10000000; i++);  // simulate work
        process_yield();
    }
}
```

So `ps`, `run`, `kill`, `suspend`, and `resume` operate on a faithful *model* of
processes — the table, the states, the queue, the scheduling policy — without the
low-level mechanism that would make them truly preemptive tasks. This is a
deliberate teaching choice: the data structures and policy of a scheduler are
worth understanding in isolation from the assembly that saves and restores
registers. The theory is in
[cooperative-scheduling.md](../concepts/cooperative-scheduling.md).

> 💡 **Tidbit:** Because there is no real context switch and scheduling is
> cooperative, `run counter` registers the process in the table but the sample
> workloads do not actually run concurrently with the shell. Treat the process
> commands as a guided tour of a scheduler's *structure*, not a multitasking
> runtime.


## Code walkthrough: the fixed-point calculator

There is no FPU math library, so `calc` represents decimals as integers scaled by
1000 — three fractional digits of precision. `parse_float` reads a number and
returns it pre-scaled:

```c
// Returns the number multiplied by 1000 (3 decimal places).
int parse_float(const char* str, int* has_decimal) {
    int result = 0, decimal_places = 0, sign = 1, i = 0;
    /* ... sign, integer part ... */
    if (str[i] == '.') {
        *has_decimal = 1; i++;
        while (str[i] >= '0' && str[i] <= '9' && decimal_places < 3) {
            result = result * 10 + (str[i] - '0');
            decimal_places++; i++;
        }
        /* skip any extra digits */
    }
    while (decimal_places < 3) {   // pad to exactly 3 fractional digits
        result *= 10; decimal_places++;
    }
    return sign * result;
}
```

So `3.14` becomes `3140` and `2` becomes `2000`. Addition and subtraction are
then exact integer operations. Multiplication and division need scale
adjustment, done carefully to avoid 32-bit overflow:

```c
case '*': {
    /* split each operand into integer and fractional parts */
    int temp_high = (num1 / 1000) * num2;            // int x whole
    int temp_low  = (num1 % 1000) * (num2 / 1000);   // frac x int
    int temp_frac = ((num1 % 1000) * (num2 % 1000)) / 1000; // frac x frac
    result = temp_high + temp_low + temp_frac;
    /* re-apply sign */
}
case '/': {
    if (num2 == 0) { println("Error: Division by zero!", RED_ON_BLACK); return; }
    int quotient    = num1 / num2;
    int remainder   = num1 % num2;
    int fractional  = (remainder * 1000) / num2;      // recover lost precision
    result = quotient * 1000 + fractional;
}
```

`float_to_str` converts the scaled integer back to a decimal string, trimming
trailing zeros. The full reasoning behind scaled-integer arithmetic — the
multiply/divide scale rule, overflow, rounding — is in
[fixed-point-arithmetic.md](../concepts/fixed-point-arithmetic.md).


## Code walkthrough: shell ergonomics

The shell keeps a fixed-size **history** ring and a flat **command table** used
both for Tab completion and as the source of truth for valid commands:

```c
const char* available_commands[] = {
    "help", "clear", "echo", "calc", "memory",
    "stats", "history", "about", "shutdown",
    "ps", "run", "kill", "suspend", "resume",
    "time", "date", "clock", "uptime", "alias", "unalias"
};
const int num_commands = 20;
```

Tab completion matches the typed prefix against that table:

```c
const char* find_completion(const char* partial, int* match_count) {
    const char* match = 0; *match_count = 0;
    for (int i = 0; i < num_commands; i++) {
        if (starts_with(available_commands[i], partial)) {
            match = available_commands[i];
            (*match_count)++;
        }
    }
    return (*match_count == 1) ? match : 0;   // unique match completes
}
```

If exactly one command matches, `get_line` completes the word in place; if
several match, `show_completions` lists them and leaves the prefix as typed. The
↑/↓ arrow keys (decoded as `0x11`/`0x12` in this stage's `get_key`) walk the
history buffer, and `history` prints it. **Aliases** let `alias name=command`
define a shorthand that is resolved to its target before dispatch.

Dispatch is still the Stage 3 `if/else if` chain over `strcmp`, just longer, with
an alias-resolution step in front:

```c
char* alias_cmd = resolve_alias(cmd);
if (alias_cmd) { /* rebuild the line from the alias + args, re-parse */ }

if      (strcmp(cmd, "help")  == 0) cmd_help();
else if (strcmp(cmd, "calc")  == 0) cmd_calc(args);
else if (strcmp(cmd, "time")  == 0) cmd_time();
/* ... 17 more ... */
else { print("Unknown command: ", RED_ON_BLACK); println(cmd, RED_ON_BLACK); }
```

Startup wires the subsystems together: `kernel_main()` calls `process_init()`
then `shell_main()`, which calls `rtc_init()` and `rtc_record_boot_time()` before
printing the banner and the current date/time. (The on-screen banner still reads
`SimpleShell OS v1.0`, carried over from Stage 3.) The complete behavior of every
command is documented in `README_SHELL.md` and in
[command-reference.md](../reference/command-reference.md).


## The 20 commands

```
help   clear   echo   calc    memory  stats   history  about  shutdown
ps     run     kill   suspend resume  time    date     clock  uptime
alias  unalias
```

| Group | Commands |
|-------|----------|
| General | `help`, `clear`, `echo`, `about`, `history`, `shutdown` |
| Calculator | `calc` |
| System info | `memory`, `stats` |
| Clock | `time`, `date`, `clock`, `uptime` |
| Processes | `ps`, `run`, `kill`, `suspend`, `resume` |
| Aliases | `alias`, `unalias` |


## How to build and run

From `helloworld-os-c-v2/`:

```sh
make            # compile shell.o, process.o, rtc.o; link; build the image
make run        # boot in QEMU
make debug      # boot under QEMU with a GDB stub (-s -S)
make clean      # remove build artifacts
```

The link step now pulls in three objects:

```sh
ld -m elf_i386 -T linker.ld -o kernel.bin \
   kernel_entry.o shell.o process.o rtc.o
cat boot.bin kernel.bin > helloworld-c.img
truncate -s 20480 helloworld-c.img
```

See [building-and-running.md](../guides/building-and-running.md) and
[toolchain-and-build.md](../reference/toolchain-and-build.md).


## What it teaches

- Reading a hardware clock over I/O ports and decoding BCD time fields.
- The *structure* of a scheduler — PCBs, states, a ready queue, a round-robin
  policy — separated from the register-level context switch.
- Doing real arithmetic with scaled integers when no floating-point library
  exists.
- The mechanics of a friendlier shell: history, prefix completion against a
  command table, and user-defined aliases.


## Known limits

- **The scheduler is a model.** No real context switch is performed and there is
  no timer preemption; tasks are cooperative and the sample workloads do not run
  concurrently with the shell.
- **RTC reads aren't fully synchronized.** A read that races a CMOS update can
  return an inconsistent field.
- **Fixed-point, not floating-point.** `calc` is limited to three decimals and
  to magnitudes that don't overflow 32-bit intermediate products.
- **Linear dispatch persists.** Twenty commands are still matched by a chain of
  `strcmp` calls.
- **`kernel.c` is dead code** in this directory — only `shell.c` is built.


## Next stage

Stage 5 is a release-engineering lesson: it trims the experimental `alias` /
`unalias` commands (20 → 18), locks the command surface, and commits the built
binaries so the artifact that boots is the artifact in version control.

→ [Stage 5 — The Stabilized Release](stage-5-release.md)


## See also

- [cmos-rtc.md](../concepts/cmos-rtc.md) — the CMOS clock, registers, and BCD
- [cooperative-scheduling.md](../concepts/cooperative-scheduling.md) — PCBs, yielding, round-robin
- [fixed-point-arithmetic.md](../concepts/fixed-point-arithmetic.md) — scaled-integer math
- [command-reference.md](../reference/command-reference.md) — every command, every stage
- [io-ports.md](../reference/io-ports.md) — `0x70`/`0x71`, `0x60`/`0x64`
- [Stage 3 — An Interactive Shell](stage-3-interactive-shell.md)
- [Home](../Home.md)
