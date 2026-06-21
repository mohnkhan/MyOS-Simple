# Command Reference

*Every shell built-in across stages 3, 4, and 5 — syntax, behavior, and which stage ships it.*

The interactive shell appears in stage 3 with 5 commands, grows to 20 in stage 4, and
settles at 18 in stage 5 (the same set as stage 4 minus `alias`/`unalias`). This page is
the authoritative cross-stage command table, verified against
[`README_SHELL.md`](../stages/stage-4-clock-processes-calc.md) and the `shell.c` command
array in [`helloworld-os-c-v3`](../stages/stage-5-release.md) (`shell.c:46-49`).

## The prompt

| Property | Value |
|----------|-------|
| Prompt text | `shell> ` |
| Prompt color | Green |
| Max input length | 128 characters |
| Tab completion | Against the command table; single match completes in place, multiple match are listed |
| History | Previously entered commands retained, shown by `history` |
| Input source | Polled [8042 keyboard](../concepts/ps2-keyboard-8042.md): status `0x64`, data `0x60` |

Color conventions: prompt green, system messages yellow, help/info cyan, errors red.

## Command matrix

| Command | Syntax | Description | Stage 3 | Stage 4 | Stage 5 |
|---------|--------|-------------|:---:|:---:|:---:|
| `help` | `help` | List all commands with one-line descriptions | ✅ | ✅ | ✅ |
| `clear` | `clear` | Clear the screen, reset cursor to top | ✅ | ✅ | ✅ |
| `echo` | `echo [text]` | Print the argument text | ✅ | ✅ | ✅ |
| `about` | `about` | Show OS name, version, system details | ✅ | ✅ | ✅ |
| `shutdown` | `shutdown` | Halt the CPU (`cli` + `hlt`) | ✅ | ✅ | ✅ |
| `calc` | `calc <a> <op> <b>` | Evaluate a binary expression (`+ - * /`) | — | ✅ | ✅ |
| `memory` | `memory` | Show memory usage / layout | — | ✅ | ✅ |
| `stats` | `stats` | Show system statistics | — | ✅ | ✅ |
| `history` | `history` | List previously entered commands | — | ✅ | ✅ |
| `ps` | `ps` | List processes (PID, name, state) | — | ✅ | ✅ |
| `run` | `run <name>` | Start a sample process (`counter`, `fibonacci`, `prime`) | — | ✅ | ✅ |
| `kill` | `kill <pid>` | Terminate a process by PID | — | ✅ | ✅ |
| `suspend` | `suspend <pid>` | Move a process to the blocked state | — | ✅ | ✅ |
| `resume` | `resume <pid>` | Return a suspended process to ready | — | ✅ | ✅ |
| `time` | `time` | Current time (HH:MM:SS) from the CMOS RTC | — | ✅ | ✅ |
| `date` | `date` | Current date from the CMOS RTC | — | ✅ | ✅ |
| `clock` | `clock` | Combined date and time | — | ✅ | ✅ |
| `uptime` | `uptime` | Time elapsed since boot | — | ✅ | ✅ |
| `alias` | `alias` / `alias name=command` | List aliases, or define one | — | ✅ | — |
| `unalias` | `unalias name` | Remove an alias | — | ✅ | — |

**Counts:** Stage 3 = 5, Stage 4 = 20, Stage 5 = 18.

## Command groups

### General

| Command | Notes |
|---------|-------|
| `help` | The discoverability entry point; lists the active command set. |
| `clear` | Resets the VGA cursor to the top-left of the [`0xB8000`](../concepts/vga-text-mode.md) framebuffer. |
| `echo` | Example: `echo Hello, World!` prints `Hello, World!`. |
| `about` | OS name and version banner. |
| `history` | Stage 4+. Lists the retained command history. |
| `shutdown` | Issues `cli` then `hlt`, freezing the CPU. |

### Calculator (stage 4+)

`calc <a> <op> <b>` — operators `+`, `-`, `*`, `/`.

```text
calc 2 + 2
calc 3.14 * 2
calc 10.5 / 2.5
```

Numbers are parsed into [fixed-point](../concepts/fixed-point-arithmetic.md) integers
**scaled by 1000** (three fractional digits), so results are accurate to 1/1000. There
is no FPU math library.

### Real-time clock (stage 4+)

`time`, `date`, `clock`, `uptime` read the [CMOS RTC](../concepts/cmos-rtc.md) through
ports `0x70`/`0x71` and convert the [BCD](glossary.md#bcd) fields to binary. `uptime`
reports elapsed time since boot.

### Process management (stage 4+)

| Command | Effect |
|---------|--------|
| `ps` | List processes with PID, name, and state. |
| `run <name>` | Start one of `counter`, `fibonacci`, `prime`. |
| `kill <pid>` | Terminate a process. |
| `suspend <pid>` | Move a process to the blocked state. |
| `resume <pid>` | Return a suspended process to ready. |

Scheduling is [cooperative round-robin](../concepts/cooperative-scheduling.md); sample
processes yield with `process_yield()`. There is no timer-driven preemption.

### Aliases (stage 4 only)

| Rule | Detail |
|------|--------|
| Define | `alias name=command` — **no spaces** around `=` |
| List | `alias` with no argument lists current aliases |
| Remove | `unalias name` |
| Collision | An alias name may not shadow a built-in command |
| Resolution | Aliases are resolved to the target command before dispatch |

```text
alias ll=ps
ll            # runs ps
unalias ll
```

Stage 5 drops `alias` and `unalias`, reducing the set to 18.

## Input editing keys

| Key | Effect |
|-----|--------|
| Printable keys | Echoed and appended to the line |
| Shift + key | Uppercase letters / shifted symbols (see [scancode tables](scancode-tables.md)) |
| Backspace | Delete the previous character |
| Tab | Complete the current word against the command table |
| Enter | Submit the line |

> 💡 **Tidbit:** Tab completion matches against the very command array shown here
> (`shell.c:46-49` in stage 5). Because stage 5 omits `alias`/`unalias` from that array,
> typing `al<Tab>` completes nothing there — but in stage 4 it would offer `alias`.

> ⚠️ **Caveat:** `calc` carries only three decimal places because the parser scales by
> exactly 1000. `calc 1 / 3` yields `0.333`, not a rounded `0.333…`; the fourth digit is
> truncated, not rounded. This is a property of the [fixed-point](../concepts/fixed-point-arithmetic.md)
> representation, not a bug.

> 💡 **Tidbit:** `shutdown` does not power the machine off — there is no ACPI driver. It
> runs `cli; hlt`, which masks interrupts and halts the CPU until the next (now-masked)
> interrupt, effectively freezing QEMU. Close the emulator window to exit.

## See also

- [Stage 3: interactive shell](../stages/stage-3-interactive-shell.md) — the 5-command shell
- [Stage 4: clock, processes, calculator](../stages/stage-4-clock-processes-calc.md) — the 20-command shell
- [Stage 5: release](../stages/stage-5-release.md) — the consolidated 18-command set
- [CMOS / RTC](../concepts/cmos-rtc.md) — backing the time commands
- [Cooperative scheduling](../concepts/cooperative-scheduling.md) — backing the process commands
- [Fixed-point arithmetic](../concepts/fixed-point-arithmetic.md) — backing `calc`
- [Scancode tables](scancode-tables.md) — what the keys map to
- [Glossary](glossary.md)
- [Home](../Home.md)
