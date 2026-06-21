[← Home](../Home.md)

# Stage 3 — An Interactive Shell

*The kernel learns to listen: a prompt, a line editor, a parser, and five commands — all without a libc.*

Stage 2 ran a fixed demo. Stage 3 turns the kernel into something you can *talk
to*: it prints a green `shell> ` prompt, lets you edit a line, parses what you
typed into a command and arguments, and dispatches to one of five built-in
commands. The boot path is identical to Stage 2; what changes is entirely in the
C kernel, which here *is* a shell.

- Directory: `os-c-with-shell/`
- Mode: 32-bit protected mode
- Language: C + NASM
- Banner on screen: `SimpleShell OS v1.0`


## What's new vs Stage 2

| | Stage 2 | Stage 3 |
|---|---|---|
| Kernel role | a UI demo | an **interactive command shell** |
| Output model | absolute `putchar_at(x, y)` | a **flowing cursor** with newline handling and scrolling |
| Scrolling | none | `scroll_screen()` when output reaches row 25 |
| Input | character echo | a **line editor** (`get_line`) with backspace and shift |
| Parsing | none | `parse_command` splits a command word from its arguments |
| String ops | inline | hand-written `strcmp` / `strncmp` |
| Commands | none | **5 built-ins**: `help`, `clear`, `echo`, `about`, `shutdown` |

`shell.c` is the kernel: `kernel_main()` does nothing but call `shell_main()`.
The bootloader is the same as Stage 2 except it loads **15 sectors** instead of
16 (`boot.asm:34`), sized to this stage's roughly 7.5 KB kernel.


## The files

| File | Role |
|------|------|
| `shell.c` | The entire kernel: VGA output with scrolling, polled keyboard, a line editor, a command parser, and five command handlers. |
| `boot.asm` | The Stage 2 bootloader, loading 15 sectors. |
| `kernel_entry.asm`, `linker.ld`, `keyboard.h`, `Makefile` | Unchanged from Stage 2 in structure. |

The boot machinery (GDT, protected-mode switch, fixed load address) is exactly
as described in [Stage 2](stage-2-c-protected-mode.md); this page focuses on the
shell.


## Code walkthrough: a flowing terminal

Stage 2 placed every character at an explicit `(x, y)`. A shell needs text that
*flows* — advancing the cursor, wrapping at the right edge, and scrolling when it
runs off the bottom. `putchar` adds all of that on top of the Stage 2
primitive:

```c
void putchar(char c, char color) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            putchar_at(' ', cursor_x, cursor_y, color);
        }
    } else {
        putchar_at(c, cursor_x, cursor_y, color);
        cursor_x++;
    }

    if (cursor_x >= 80) {       // wrap at the right edge
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= 25) {       // ran off the bottom -> scroll
        scroll_screen();
        cursor_y = 24;
    }
}
```

`scroll_screen` copies every row up by one and clears the bottom row, working
directly on the `0xB8000` framebuffer:

```c
void scroll_screen() {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 80; x++) {
            int src = ((y + 1) * 80 + x) * 2;
            int dst = ( y      * 80 + x) * 2;
            video[dst]     = video[src];
            video[dst + 1] = video[src + 1];
        }
    }
    for (int x = 0; x < 80; x++) {        // clear last row
        int off = (24 * 80 + x) * 2;
        video[off]     = ' ';
        video[off + 1] = WHITE_ON_BLACK;
    }
}
```

`print` and `println` are thin loops over `putchar`. See
[vga-text-mode.md](../concepts/vga-text-mode.md) for the framebuffer layout.


## Code walkthrough: the line editor

`get_key` is a trimmed version of Stage 2's reader — it polls the controller,
handles the release bit, tracks only Shift, and returns either a translated
character or `0` for events to ignore. `get_line` builds an editable line on top
of it:

```c
void get_line(char* buffer, int max_len) {
    int pos = 0;
    char key;
    while (pos < max_len - 1) {
        key = get_key();
        if (key == 0) continue;

        if (key == '\n') {                  // Enter: finish the line
            buffer[pos] = '\0';
            putchar('\n', WHITE_ON_BLACK);
            break;
        } else if (key == '\b') {           // Backspace: erase one char
            if (pos > 0) {
                pos--;
                putchar('\b', WHITE_ON_BLACK);
            }
        } else if (key >= 32 && key <= 126) {  // printable: echo + store
            buffer[pos++] = key;
            putchar(key, WHITE_ON_BLACK);
        }
    }
    buffer[pos] = '\0';
}
```

Only printable ASCII (32–126) is accepted into the buffer; Enter terminates,
Backspace deletes, everything else is dropped. This is the smallest line editor
that still feels like a real prompt. Scancode handling is detailed in
[scancodes.md](../concepts/scancodes.md) and
[ps2-keyboard-8042.md](../concepts/ps2-keyboard-8042.md).

> 💡 **Tidbit:** Backspace here both moves the buffer position back *and* paints a
> space at the new cursor location (via `putchar`'s `'\b'` case), so the deleted
> character actually disappears from the screen rather than just moving the
> cursor. A surprising amount of "feels like a terminal" is small details like
> this.


## Code walkthrough: parsing and dispatch

A typed line is split into a command word and an argument string by
`parse_command`:

```c
void parse_command(char* input, char* cmd, char* args) {
    int i = 0, j = 0;
    while (input[i] == ' ') i++;            // skip leading spaces
    while (input[i] && input[i] != ' ')     // first word -> cmd
        cmd[j++] = input[i++];
    cmd[j] = '\0';
    while (input[i] == ' ') i++;            // skip spaces
    j = 0;
    while (input[i])                        // the rest -> args
        args[j++] = input[i++];
    args[j] = '\0';
}
```

There is no `libc`, so even `strcmp` is hand-written:

```c
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}
```

The shell's main loop prints the prompt, reads a line, parses it, and runs an
`if/else if` chain of `strcmp` comparisons:

```c
void shell_main() {
    char input[MAX_CMD_LEN], cmd[64], args[MAX_CMD_LEN];
    clear_screen();
    println("SimpleShell OS v1.0", YELLOW_ON_BLACK);
    println("Type 'help' for available commands", CYAN_ON_BLACK);
    println("", WHITE_ON_BLACK);

    while (1) {
        print("shell> ", GREEN_ON_BLACK);
        get_line(input, MAX_CMD_LEN);
        parse_command(input, cmd, args);

        if (cmd[0] == '\0') {
            continue;
        } else if (strcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (strcmp(cmd, "clear") == 0) {
            clear_screen();
        } else if (strcmp(cmd, "echo") == 0) {
            cmd_echo(args);
        } else if (strcmp(cmd, "about") == 0) {
            cmd_about();
        } else if (strcmp(cmd, "shutdown") == 0) {
            shutdown();
        } else {
            print("Unknown command: ", RED_ON_BLACK);
            println(cmd, RED_ON_BLACK);
        }
    }
}
```

The five commands are deliberately tiny:

| Command | Effect |
|---------|--------|
| `help` | List the available commands. |
| `clear` | Clear the screen and reset the cursor. |
| `echo [text]` | Print the argument string in green. |
| `about` | Show the OS name, version, and feature list. |
| `shutdown` | Clear the screen, print "System halted.", then `cli; hlt`. |

A full command reference for all stages is in
[command-reference.md](../reference/command-reference.md).

> ⚠️ **Caveat:** Dispatch is a linear chain of `strcmp` calls, and `cmd` is
> copied into a fixed 64-byte buffer with no bounds check in `parse_command`. For
> a five-command shell driven by a 128-byte line this is fine, but it does not
> scale and does not defend against a pathologically long token. Stage 4 keeps
> the same dispatch style even as the command count quadruples.


## How to build and run

From `os-c-with-shell/`:

```sh
make            # build the shell image
make run        # boot the shell in QEMU
make debug      # boot under QEMU with a GDB stub (-s -S)
make clean      # remove build artifacts
```

The build is the same shape as Stage 2 — freestanding `gcc`, `nasm` for the
stubs, `ld` with the linker script, then `cat boot.bin kernel.bin` into the
image — except `shell.o` takes the place of `kernel.o`. See
[building-and-running.md](../guides/building-and-running.md).


## What it teaches

- How a flowing terminal is built from an absolute-position `putchar_at`: cursor
  tracking, line wrap, and scrolling.
- A minimal but usable line editor over a polled keyboard.
- Tokenizing a command line and dispatching to handlers by string comparison.
- Writing the string primitives (`strcmp`, `strncmp`) you normally take for
  granted, because there is no standard library underneath you.


## Known limits

- **Five commands, linear dispatch.** No command table yet; each command is an
  `else if` branch.
- **No history, completion, or aliases.** Typing is single-line and immediate.
- **Only Shift among modifiers.** Ctrl/Alt/Caps from Stage 2's reader are not
  tracked here.
- **No bounds check on the command token.** The `cmd` buffer is fixed at 64
  bytes and trusts the input length.


## Next stage

Stage 4 asks "what does an OS actually *do*?" and answers with real subsystems: a
CMOS real-time clock, a cooperative process model, a fixed-point calculator, and
shell ergonomics (history, tab-completion, aliases) — growing from 5 commands to
20.

→ [Stage 4 — Clock, Processes, and a Calculator](stage-4-clock-processes-calc.md)


## See also

- [vga-text-mode.md](../concepts/vga-text-mode.md) — the framebuffer and scrolling
- [ps2-keyboard-8042.md](../concepts/ps2-keyboard-8042.md) — polling the keyboard controller
- [scancodes.md](../concepts/scancodes.md) — Set-1 scancode translation
- [command-reference.md](../reference/command-reference.md) — every command across stages
- [scancode-tables.md](../reference/scancode-tables.md) — the lookup tables in `keyboard.h`
- [building-and-running.md](../guides/building-and-running.md) — build and boot any stage
- [Stage 2 — A C Kernel in Protected Mode](stage-2-c-protected-mode.md)
- [Home](../Home.md)
