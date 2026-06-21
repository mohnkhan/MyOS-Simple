# Writing Your Own Stage

*You have read all five stages — now extend the OS. Add a command, grow the kernel without crashing it, and learn exactly where to hook into boot and kernel.*

The best way to understand MyOS-Simple is to change it. This guide walks through
the two most common extensions — **adding a shell command** and **growing the
kernel safely** — and then shows the hook points for deeper work. Everything here
is verified against the real source; copy-paste and adapt.

The natural place to experiment is a copy of stage 3, which is the smallest stage
with an interactive shell:

```sh
cp -r os-c-with-shell my-stage
cd my-stage
make && make run        # confirm the baseline builds and boots
```

## Add a shell command

On stage 3 the shell dispatches commands with a chain of `strcmp` checks in
`shell_main` (`os-c-with-shell/shell.c:285-300`):

```c
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
```

Adding a command is two edits.

### 1. Write the command function

Put it alongside the other `cmd_*` implementations (near `shell.c:234`). Use the
existing `print` / `println` helpers, which take a color attribute
(`shell.c:107-118`):

```c
void cmd_hello(char* args) {
    if (args[0] == '\0') {
        println("Hello from your own command!", GREEN_ON_BLACK);
    } else {
        print("Hello, ", GREEN_ON_BLACK);
        println(args, YELLOW_ON_BLACK);
    }
}
```

`args` is the rest of the input line after the command word, already split out by
`parse_command` (`shell.c:210-231`). The color constants (`GREEN_ON_BLACK`,
`YELLOW_ON_BLACK`, …) are defined at `shell.c:27-30`.

### 2. Add it to the dispatcher

Insert another `else if` branch into the chain in `shell_main`:

```c
} else if (strcmp(cmd, "hello") == 0) {
    cmd_hello(args);
}
```

Rebuild and try it:

```sh
make && make run
# at the prompt:
shell> hello world
Hello, world
```

That is the whole pattern on stage 3.

> 💡 **Tidbit:** Stage 3 has no command list or `help` table beyond the hard-coded
> text in `cmd_help` (`shell.c:234-241`). If you want your command to show up in
> `help`, add a line there too — it is just printed text, not a registry.

### On stages 4 and 5 there is one extra step

Stages 4 and 5 keep a **command-name array** used for Tab completion and help, in
addition to the dispatcher. If you add a command there, you must add its name to
that array as well, or Tab completion and help will be out of sync with what the
shell actually accepts. Stage 4 has **20** commands; stage 5 has **18**. Keep the
count consistent: add the `cmd_*` function, the dispatcher branch, *and* the array
entry.

> ⚠️ **Caveat:** Forgetting the name array on stage 4/5 produces a subtle bug —
> the command works when typed in full, but never appears in `help` and never
> completes on Tab. If a new command "works but won't autocomplete," the array is
> what you missed.

## Growing the kernel safely

This is the one thing that will bite you. The bootloader does **not** read until
end-of-file; it reads a **fixed number of sectors** hard-coded in `boot.asm`:

```asm
mov dh, 15          ; Load 15 sectors (enough for ~7.5KB kernel)
```

(`os-c-with-shell/boot.asm:34`.) Per stage the count is:

| Stage | Sectors loaded | Max kernel size | Source |
|-------|----------------|-----------------|--------|
| 2 | 16 | 8 KiB | `helloworld-os-c/boot.asm:35` |
| 3 | 15 | 7.5 KiB | `os-c-with-shell/boot.asm:34` |
| 4 | 39 | 19.5 KiB | `helloworld-os-c-v2/boot.asm:35` |
| 5 | 39 | 19.5 KiB | `helloworld-os-c-v3/boot.asm:35` |

If your kernel grows past `N × 512` bytes, the loader drops the tail with **no
error**. Execution runs into missing code and the machine triple-faults (a reboot
loop in QEMU). The fix is mechanical.

### Step 1 — check the current kernel size

```sh
make
ls -l kernel.bin
```

Compare against `N × 512`. For stage 3, 15 sectors = 7680 bytes. If `kernel.bin`
is comfortably under that, you have room and need do nothing.

### Step 2 — raise the sector count in `boot.asm`

Edit the `mov dh, N` line to a larger value. For example, to allow ~16 KiB on
stage 3, bump it to 32 sectors:

```asm
mov dh, 32          ; Load 32 sectors (~16 KiB kernel)
```

Choose a count with headroom so you do not have to keep editing it.

### Step 3 — raise the image padding in the `Makefile`

The image must be large enough to *contain* every sector the loader will read.
The Makefile pads with `truncate` (`os-c-with-shell/Makefile:44`):

```make
truncate -s 10240 $(IMAGE)      # 10240 = 20 sectors
```

The padded size must be at least **(1 boot sector + N kernel sectors) × 512**.
For 32 kernel sectors you need at least 33 × 512 = 16896 bytes; round up to a
clean number, e.g.:

```make
truncate -s 17408 $(IMAGE)      # 34 sectors, comfortably covers 1 + 32
```

(Stages 4 and 5 already use `truncate -s 20480`, 40 sectors, for their 39-sector
load — see `helloworld-os-c-v2/Makefile:52`.)

### Step 4 — rebuild and boot

```sh
make clean && make && make run
```

If it boots, you have headroom. If you ever see a fresh reboot loop right after
adding code, this is the first thing to re-check — see
[troubleshooting.md](troubleshooting.md#kernel-grew-too-large).

> ⚠️ **Caveat:** Bump **both** numbers together. Raising the sector count without
> raising the image size makes the BIOS read past end-of-file → `Disk read
> error!`. Raising the image size without raising the sector count means the new
> sectors are never loaded → silent truncation. They are a matched pair.

## Where to hook in

A map of the moving parts, so you know which file owns which job.

### The boot sector — `boot.asm`

Loaded by the BIOS to `0x7C00` in 16-bit real mode. It sets up segments and the
stack, reads the kernel via BIOS `int 0x13`, installs a flat 3-entry GDT, sets
`CR0.PE`, and far-jumps to 32-bit code, which reloads the data selectors, sets
`ESP = EBP = 0x90000`, and calls the kernel at `0x1000`
(`helloworld-os-c/boot.asm:33-63`).

Touch `boot.asm` when you need to change *how much* is loaded (the sector count),
the GDT, or the protected-mode environment. Most kernel work does **not** require
touching it.

### The entry stub — `kernel_entry.asm`

The 32-bit assembly shim that the boot sector calls. It hands control to the C
entry point. You rarely change this.

### The kernel — `shell.c` (stage 3) / `kernel.c` (stage 2)

Your code lives here. Execution begins at the C entry point and runs forever. Key
facilities already written for you (no libc exists, so these are hand-rolled):

- **Output:** `print` / `println` with a VGA color attribute (`shell.c:107-118`),
  writing directly to VGA memory at `0xB8000` (`shell.c:13, 44-49`).
- **Input:** `get_key` / `get_line` polling the keyboard at ports `0x60`/`0x64`
  (`shell.c:140-207`).
- **Strings:** `strcmp`, `strncmp`, `parse_command` (`shell.c:121-137, 210-231`).

### The linker script — `linker.ld`

Places the kernel at the fixed load address and emits a flat binary:
`OUTPUT_FORMAT(binary)` and `. = 0x1000` (`helloworld-os-c/linker.ld:11, 15`).
Because the output is a flat binary, **`0x1000` is both the load address and the
entry point — there is no relocation**. If you change the load address you must
change it in `boot.asm`, `linker.ld`, and your GDB `add-symbol-file` offset
together.

### Adding a new C source file

If you split your code into a new `.c` file (as stages 4/5 do with `process.c`
and `rtc.c`), add a compile rule and include the new `.o` in the link line, just
like `helloworld-os-c-v2/Makefile:37-46`:

```make
mymod.o: mymod.c
	$(CC) $(CFLAGS) mymod.c -o mymod.o

kernel.bin: kernel_entry.o shell.o mymod.o
	$(LD) $(LDFLAGS) -o kernel.bin kernel_entry.o shell.o mymod.o
```

Remember every new file adds to `kernel.bin` — watch the sector budget above.

> 💡 **Tidbit:** The whole memory plan is fixed and paging is never enabled, so
> every address is physical and predictable: kernel at `0x1000`, stack top at
> `0x90000`, VGA at `0xB8000`. You can write to any of these directly from C with
> a `volatile` pointer — there is no virtual memory in the way.

## A suggested first project

1. Copy stage 3 to `my-stage` and confirm it builds.
2. Add a `uptime`-style command that counts loop iterations, or a `color`
   command that takes an argument and re-prints in a chosen attribute.
3. When the kernel starts to grow, practice the sector-count bump so the
   procedure is muscle memory before you need it.
4. Move to stage 4/5 patterns (the command-name array, multiple `.c` modules)
   once the basics feel comfortable.

## See also

- [building-and-running.md](building-and-running.md) — the build pipeline you are extending
- [debugging-with-gdb.md](debugging-with-gdb.md) — when your new code crashes
- [troubleshooting.md](troubleshooting.md) — truncation, triple faults, disk errors
- [../stages/stage-3-interactive-shell.md](../stages/stage-3-interactive-shell.md) — the shell you are modifying
- [../stages/stage-4-clock-processes-calc.md](../stages/stage-4-clock-processes-calc.md) — the command-name array pattern
- [../concepts/vga-text-mode.md](../concepts/vga-text-mode.md) — how `print` reaches the screen
- [../concepts/linker-scripts.md](../concepts/linker-scripts.md) — the flat-binary load address
- [../reference/command-reference.md](../reference/command-reference.md) — the existing command set
- [../reference/memory-map.md](../reference/memory-map.md) — the fixed addresses to build on
- [../Home.md](../Home.md) — wiki home
