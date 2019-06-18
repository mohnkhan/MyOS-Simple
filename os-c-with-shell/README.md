# Stage 3 — Interactive Command Shell

This stage gives the kernel a voice. On top of the protected-mode C foundation
from `helloworld-os-c`, it adds direct keyboard input (polling the 8042
controller) and a small command interpreter with a colored prompt, line editing,
and five built-in commands. A kernel that only prints is a demonstration; a
kernel you can type at is a system.


## Position in the series

`helloworld-os-c` established the boot path, GDT, protected-mode switch, and
direct VGA output. This stage reuses all of that and replaces the static color
demo with an input loop and a command dispatcher. The kernel entry point is now
`shell_main` (see `kernel_entry.asm`) instead of `kernel_main`. Stage 4
(`helloworld-os-c-v2`) expands this single shell into a much larger system with a
clock, processes, and a calculator.


## What it does

Presents a prompt:

```
shell>
```

and reads a line of input, then dispatches on the first word. Display features:

- 16-color VGA text output at 80×25.
- Screen scrolling when output reaches the bottom row.
- Color-coded output: system messages in yellow, the prompt in green, help text
  in cyan, errors in red.

Line editing:

- Character echo as you type.
- `Backspace` deletes the previous character.
- Shift handling for uppercase and symbols.
- Maximum command length 128 characters.


## Built-in commands

| Command | Description |
|---------|-------------|
| `help` | List the available commands and what they do. |
| `clear` | Clear the screen and reset the cursor to the top. |
| `echo [text]` | Print the supplied text. Example: `echo Hello, World!` |
| `about` | Show OS name, version, and basic system information. |
| `shutdown` | Halt the system (`cli` + `hlt`). |

Unrecognized input prints an error in red and returns to the prompt.


## How input works

The shell reads the keyboard by polling the Intel 8042 keyboard controller
rather than using BIOS services (which are unavailable in protected mode):

1. Poll status port `0x64`; wait until the output-buffer-full bit (`0x01`) is
   set.
2. Read the scancode from data port `0x60`.
3. Translate the scancode to ASCII using the tables in `keyboard.h`, honoring
   the shift state, and ignore key-release events (scancodes with the high bit
   set).

All string handling (`strcmp`, length, comparison, the command table) is written
by hand because there is no standard library.


## Source layout

```
os-c-with-shell/
├── boot.asm           Bootloader (loads 15 sectors), GDT, switch to PM.
├── boot_simple.asm    Standalone 512-byte assembly image.
├── shell.c            The command shell: input loop + built-ins.
├── kernel.c           Legacy stage-2 kernel, retained for reference (not built).
├── keyboard.h         Scancode tables and keyboard port/flag definitions.
├── kernel_entry.asm   32-bit entry stub: _start -> shell_main.
├── linker.ld          Places the kernel at 0x1000, output flat binary.
├── Makefile           Build/run/debug targets.
└── *.bin / *.img      Build outputs.
```

Note: `kernel.c` is the previous stage's kernel kept for comparison. The
`Makefile` compiles and links `shell.c` (as `shell.o`), not `kernel.c`.


## Boot specifics

The bootloader is the same design as stage 2 with one change: it loads **15
sectors** (`mov dh, 15`) instead of 2, because the shell kernel is larger. The
final image is concatenated (`cat boot.bin kernel.bin`) and padded with
`truncate -s 10240`. Load address (`0x1000`), stack (`0x90000`), and the flat
GDT are unchanged — see the `helloworld-os-c` README for the full boot walkthrough.


## Prerequisites

```sh
sudo apt install nasm gcc gcc-multilib binutils make qemu-system-x86
```


## Build

```sh
make            # build helloworld-c.img (shell) and helloworld-simple.img
make clean      # remove *.bin *.o *.img
```

Build steps:

```sh
nasm -f bin   boot.asm        -o boot.bin
nasm -f elf32 kernel_entry.asm -o kernel_entry.o
gcc -m32 -ffreestanding -fno-pic -fno-pie -nostdlib -nostdinc \
    -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs \
    -Wall -Wextra -c shell.c -o shell.o
ld  -m elf_i386 -T linker.ld -o kernel.bin kernel_entry.o shell.o
cat boot.bin kernel.bin > helloworld-c.img
truncate -s 10240 helloworld-c.img
nasm -f bin boot_simple.asm -o helloworld-simple.img
```

See the `helloworld-os-c` README for a flag-by-flag explanation of the compiler
options and the linker script.


## Run

```sh
make run          # boot the shell
make run-simple   # boot the pure-assembly simple image
make debug        # boot under QEMU with a GDB stub (-s -S)
```

Once booted, type `help` to list commands. Try `echo`, `about`, `clear`, then
`shutdown` to halt.


## Notes and limitations

- Input is polled, not interrupt-driven; the CPU spins waiting for keystrokes.
- No command history, pipes, arguments beyond a single trailing string, or job
  control — those arrive in later stages or not at all.
- `shutdown` halts the CPU; close QEMU to exit.
