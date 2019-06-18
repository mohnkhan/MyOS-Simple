# Shell Command Reference (v2)

The interactive command interpreter for stage 4. This document is the complete
reference for the shell's behavior and its 20 built-in commands. For the
architecture and build instructions, see `README.md` in this directory.


## The prompt

```
shell>
```

The prompt is printed in green. Type a command and press `Enter`. The shell
splits the line into a command word and an argument string, resolves any alias,
and dispatches to the matching handler. Unknown commands produce a red error.


## Input and editing

| Key | Effect |
|-----|--------|
| Printable keys | Echoed and appended to the current line |
| `Shift` + key | Uppercase letters and shifted symbols |
| `Backspace` | Delete the previous character |
| `Tab` | Complete the current word against the command table |
| `Enter` | Submit the line for execution |

- **Maximum line length:** 128 characters.
- **Tab-completion:** if exactly one command matches the typed prefix it is
  completed in place; if several match, they are listed and the prefix is kept.
- **History:** previously entered commands are retained and viewable with
  `history`.

Input is read by polling the 8042 keyboard controller (status port `0x64`, data
port `0x60`) and translating scancodes via `keyboard.h`; key-release events are
ignored.


## Commands

### General

| Command | Syntax | Description |
|---------|--------|-------------|
| `help` | `help` | List all commands with one-line descriptions. |
| `clear` | `clear` | Clear the screen, reset the cursor to the top. |
| `echo` | `echo [text]` | Print the argument text. Example: `echo Hello, World!` |
| `about` | `about` | Show OS name, version, and system details. |
| `history` | `history` | List previously entered commands. |
| `shutdown` | `shutdown` | Halt the CPU (`cli` + `hlt`). |

### Calculator

| Command | Syntax | Description |
|---------|--------|-------------|
| `calc` | `calc <a> <op> <b>` | Evaluate a binary expression. Operators: `+ - * /`. Decimals supported. |

Examples:

```
calc 2 + 2
calc 3.14 * 2
calc 10.5 / 2.5
```

Numbers are parsed into fixed-point integers scaled by 1000 (three fractional
digits), so results are accurate to 1/1000.

### System information

| Command | Syntax | Description |
|---------|--------|-------------|
| `memory` | `memory` | Show memory usage / layout information. |
| `stats` | `stats` | Show system statistics. |

### Real-time clock

| Command | Syntax | Description |
|---------|--------|-------------|
| `time` | `time` | Current time (HH:MM:SS) from the CMOS RTC. |
| `date` | `date` | Current date from the CMOS RTC. |
| `clock` | `clock` | Combined date and time. |
| `uptime` | `uptime` | Time elapsed since boot. |

Time is read from CMOS via ports `0x70`/`0x71` and converted from BCD.

### Process management

| Command | Syntax | Description |
|---------|--------|-------------|
| `ps` | `ps` | List processes with PID, name, and state. |
| `run` | `run <name>` | Start one of the sample processes (counter, fibonacci, prime). |
| `kill` | `kill <pid>` | Terminate a process by PID. |
| `suspend` | `suspend <pid>` | Move a process to the blocked state. |
| `resume` | `resume <pid>` | Return a suspended process to ready. |

Scheduling is cooperative round-robin; sample processes yield with
`process_yield()`.

### Aliases

| Command | Syntax | Description |
|---------|--------|-------------|
| `alias` | `alias` / `alias name=command` | List aliases, or define a new one. |
| `unalias` | `unalias name` | Remove an alias. |

Rules:

- Syntax is `alias name=command` with no spaces around `=`.
- An alias name may not collide with a built-in command.
- Aliases are resolved to their target command before dispatch.

Example:

```
alias ll=ps
ll            # runs ps
unalias ll
```

> Note: `alias` and `unalias` exist only in this stage (v2). Stage 5
> (`helloworld-os-c-v3`) removes them, reducing the command set to 18.


## Complete command list (20)

```
help   clear   echo   calc    memory  stats   history  about  shutdown
ps     run     kill   suspend resume  time    date     clock  uptime
alias  unalias
```


## Display

- 80×25 VGA text mode, 16 colors, written directly to `0xB8000`.
- Output scrolls when it reaches the bottom row.
- Color conventions: prompt green, system messages yellow, help/info cyan,
  errors red.
