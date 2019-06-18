# Stage 4 — Shell Becomes a System (v2)

This stage turns the interactive shell from `os-c-with-shell` into a small but
genuine operating system. The shell is now the front end to several kernel
subsystems: a CMOS real-time clock, a cooperative process model, a
floating-point calculator built without a math library, command history, and
ergonomic input (tab-completion and user-defined aliases). It exposes **20
built-in commands**.

For the complete command reference, see `README_SHELL.md` in this directory.


## Position in the series

`os-c-with-shell` proved the shell concept with five commands. This stage keeps
the same boot path and protected-mode foundation but grows the codebase roughly
sevenfold (the shell alone is ~1,300 lines) and splits functionality into
modules: `shell.c`, `rtc.c`, and `process.c`. Stage 5 (`helloworld-os-c-v3`) is
a stabilized version of this one: it removes the experimental `alias`/`unalias`
commands (20 down to 18) and commits the built artifacts.


## Subsystems

### Real-time clock (`rtc.c`, `rtc.h`)

Reads wall-clock time and date from the CMOS RTC over I/O ports `0x70`
(register select) and `0x71` (data):

- Selects a CMOS register, reads the value, and converts from BCD to binary.
- Provides seconds, minutes, hours, day, month, year, and weekday via
  `rtc_read_time()`.
- Records a boot timestamp and computes uptime as the difference between the
  current time and that timestamp.

Commands: `time`, `date`, `clock`, `uptime`.

### Process model (`process.c`, `process.h`)

A cooperative multitasking model with a process control block (PCB) per task:

- Up to `MAX_PROCESSES` (10) processes, each with a PID, name, state
  (`READY`, `RUNNING`, `BLOCKED`, `TERMINATED`), saved CPU context, stack,
  priority, and a `next` pointer forming the run queue.
- A round-robin scheduler (`process_scheduler`) selects the next ready process;
  tasks hand off the CPU voluntarily with `process_yield()`. There is no
  timer-driven preemption.
- Sample workloads are included: a counter, a Fibonacci generator, and a prime
  finder.

Commands: `ps`, `run`, `kill`, `suspend`, `resume`.

### Floating-point calculator (`shell.c`)

Evaluates simple `operand operator operand` expressions with decimal support,
without linking a math library:

- `parse_float()` parses a decimal string into a fixed-point integer scaled by
  1000 (three fractional digits).
- `float_to_str()` formats a scaled integer back into a decimal string.
- Supports `+`, `-`, `*`, `/`. Examples: `calc 3.14 * 2`, `calc 10.5 / 2.5`.

Command: `calc`.

### Shell ergonomics (`shell.c`)

- **Command history** — recall previously entered commands (`history`).
- **Tab-completion** — pressing `Tab` completes against the command table; an
  ambiguous prefix lists the matching commands.
- **Aliases** — `alias name=command` defines a shortcut, `unalias name` removes
  it; aliases are checked for conflicts with built-ins and resolved before
  dispatch. (These two commands are removed in stage 5.)

Commands: `history`, `memory`, `stats`, `alias`, `unalias`.


## Full command set (20)

```
help   clear   echo   calc    memory  stats   history  about  shutdown
ps     run     kill   suspend resume  time    date     clock  uptime
alias  unalias
```

See `README_SHELL.md` for per-command syntax and examples.


## Source layout

```
helloworld-os-c-v2/
├── boot.asm           Bootloader (loads 39 sectors), GDT, switch to PM.
├── boot_simple.asm    Standalone 512-byte assembly image.
├── shell.c            Shell: input loop, dispatch, calc, history, aliases, tab-complete.
├── process.c/.h       Cooperative process model and scheduler.
├── rtc.c/.h           CMOS real-time clock and uptime.
├── kernel.c           Legacy stage-2 kernel, retained for reference (not built).
├── kernel.h           VGA helpers and shared kernel I/O inlines.
├── keyboard.h         Scancode tables and keyboard port/flag definitions.
├── stdint.h / stddef.h  Minimal freestanding type definitions.
├── kernel_entry.asm   32-bit entry stub: _start -> shell_main.
├── linker.ld          Places the kernel at 0x1000, output flat binary.
└── Makefile           Build/run/debug targets.
```


## Boot specifics

Same boot design as the earlier C stages, with two adjustments for the larger
kernel:

- The bootloader loads **39 sectors** (`mov dh, 39`, ~19.5 KiB) instead of 15.
- The final image is padded to **20480 bytes** (`truncate -s 20480`) — 40
  sectors — to accommodate the RTC and process modules.

Load address `0x1000`, stack `0x90000`, and the flat three-entry GDT are
unchanged. See the `helloworld-os-c` README for the full boot walkthrough.


## Prerequisites

```sh
sudo apt install nasm gcc gcc-multilib binutils make qemu-system-x86
```


## Build

```sh
make            # build helloworld-c.img and helloworld-simple.img
make clean      # remove *.bin *.o *.img
```

Build steps (note three C modules are compiled and linked together):

```sh
nasm -f bin   boot.asm         -o boot.bin
nasm -f elf32 kernel_entry.asm -o kernel_entry.o

gcc -m32 -ffreestanding -fno-pic -fno-pie -nostdlib -nostdinc \
    -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs \
    -Wall -Wextra -c shell.c   -o shell.o
gcc ... -c process.c -o process.o
gcc ... -c rtc.c     -o rtc.o

ld -m elf_i386 -T linker.ld -o kernel.bin \
    kernel_entry.o shell.o process.o rtc.o

cat boot.bin kernel.bin > helloworld-c.img
truncate -s 20480 helloworld-c.img
nasm -f bin boot_simple.asm -o helloworld-simple.img
```

See the `helloworld-os-c` README for a flag-by-flag explanation of the compiler
options and the linker script. The link order matters only in that
`kernel_entry.o` (containing `_start`) must be present; the linker script places
`.text` from `0x1000`.

Two benign warnings are expected from `-Wextra` (an unused variable in
`process.c` and an unused helper in `rtc.c`); they do not affect the build.


## Run

```sh
make run          # boot the full system
make run-simple   # boot the pure-assembly simple image
make debug        # boot under QEMU with a GDB stub (-s -S)
```

After boot, useful first commands: `help`, `clock`, `calc 10.5 / 2.5`, `ps`,
`uptime`.


## Notes and limitations

- Multitasking is cooperative: a process that never calls `process_yield()`
  starves the others. There is no preemption and no inter-process memory
  protection.
- The calculator is fixed-point (three fractional digits), not true IEEE-754
  floating point; results are rounded to the 1/1000 scale.
- RTC reads are not guarded against the CMOS update-in-progress flag, so a read
  that races an update may briefly return an inconsistent field.
- `context_switch` is declared as an external assembly routine for the process
  model; the cooperative scheduler operates within the shell's context.
