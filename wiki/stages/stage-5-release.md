# Stage 5 — The Stabilized Release

*Stabilize and ship: trim the experiments, lock the command surface, and commit the artifact that actually boots.*

The first four stages added capability. Stage 5 adds *discipline*. Architecturally
it is Stage 4 consolidated for release: the experimental `alias` / `unalias`
commands are removed (the command set drops from 20 to 18), the command surface is
locked, and the actual built binaries — `boot.bin`, `kernel.bin`, and the bootable
`.img` images — are committed to the repository, so the thing that boots is the
thing under version control. Nothing new is invented here; the lesson is what *not*
to ship and how to make a build reproducible.

- Directory: `helloworld-os-c-v3/`
- Mode: 32-bit protected mode
- Language: C (multiple modules) + NASM
- Built kernel: `shell.c` (~1,121 lines after the alias machinery is removed)


## What's new vs Stage 4

Stage 5 is the *only* stage whose headline change is removal and process rather
than a new feature.

| | Stage 4 (v2) | Stage 5 (v3) |
|---|---|---|
| Commands | 20 | **18** |
| `alias` / `unalias` | present | **removed** |
| `shell.c` | ~1,322 lines | ~1,121 lines |
| `rtc.c`, `process.c`, headers | — | **byte-for-byte identical to v2** |
| Sectors loaded | 39 | 39 (unchanged) |
| Built artifacts in the repo | not committed | **committed** |

Everything else — the boot path, the `0x1000` load address, the `0x90000` stack,
the CMOS RTC, the cooperative process model, the fixed-point calculator,
history, and Tab completion — is carried over unchanged from
[Stage 4](stage-4-clock-processes-calc.md). The only source file that differs from
v2 is `shell.c`.


## The files

The layout matches Stage 4 exactly:

| File | Role |
|------|------|
| `shell.c` | The kernel and shell, with the alias machinery removed (18 commands). |
| `process.c` / `process.h` | The process model — identical to v2. |
| `rtc.c` / `rtc.h` | The CMOS clock — identical to v2. |
| `keyboard.h`, `kernel.h`, `stdint.h`, `stddef.h` | Shared headers — identical to v2. |
| `README_SHELL.md` | The command reference, updated to 18 commands. |
| `boot.bin`, `kernel.bin`, `*.o`, `*.img` | **Committed build artifacts** (see below). |
| `kernel.c` | Unused reference copy, as in v2 — not built. |


## What changed in the code

The command table loses its last two entries:

```c
const char* available_commands[] = {
    "help", "clear", "echo", "calc", "memory",
    "stats", "history", "about", "shutdown",
    "ps", "run", "kill", "suspend", "resume",
    "time", "date", "clock", "uptime"
};
const int num_commands = 18;
```

Because Tab completion and the dispatcher both read this table, removing the two
strings — together with the alias structs, `resolve_alias`, `cmd_alias`,
`cmd_unalias`, and the alias-resolution step at the top of the dispatch loop — is
all it takes to drop the feature cleanly. Tab completion now offers 18
candidates; the `if/else if` chain has two fewer branches. The subsystems that
back the remaining commands (`rtc.c`, `process.c`) were not touched at all.

> 💡 **Tidbit:** Aliases were a fine experiment, but they widened the surface the
> shell has to defend (alias-name collisions with built-ins, re-parsing a
> rebuilt command line) for a convenience feature. Cutting them for the release
> is the small, real version of a decision every shipping project makes:
> *fewer features, each fully owned,* beats more features half-owned.


## The 18 commands

```
help   clear   echo   calc    memory  stats   history  about  shutdown
ps     run     kill   suspend resume  time    date     clock  uptime
```

| Group | Commands |
|-------|----------|
| General | `help`, `clear`, `echo`, `about`, `history`, `shutdown` |
| Calculator | `calc` |
| System info | `memory`, `stats` |
| Clock | `time`, `date`, `clock`, `uptime` |
| Processes | `ps`, `run`, `kill`, `suspend`, `resume` |

Behavior of each is unchanged from Stage 4 and documented in
[command-reference.md](../reference/command-reference.md) and the stage's
`README_SHELL.md`.


## The release-engineering idea: committed artifacts

The defining move of this stage is that the compiled outputs are committed
alongside the source:

```text
helloworld-os-c-v3/
├── shell.c, process.c, rtc.c, *.h   # source
├── boot.bin                         # assembled boot sector  (committed)
├── kernel.bin                       # linked kernel          (committed)
├── kernel_entry.o, shell.o,         # object files           (committed)
│   process.o, rtc.o
├── helloworld-c.img                 # bootable disk image     (committed)
└── helloworld-simple.img            # pure-asm image          (committed)
```

Committing build outputs is unusual for a source repository — normally `.bin`,
`.o`, and `.img` files are generated and git-ignored. Here it is deliberate: a
reader can clone the repo and **boot the exact tested artifact in QEMU without
installing a toolchain at all**:

```sh
qemu-system-x86_64 -drive format=raw,file=helloworld-os-c-v3/helloworld-c.img
```

The trade-off is the usual one for vendored binaries — the repository carries
build state that can drift from source if someone edits `shell.c` and forgets to
rebuild. The point of the stage is to make that trade-off *consciously*: for a
teaching artifact whose value is "you can see and run the finished thing," the
reproducibility is worth the redundancy.

> ⚠️ **Caveat:** Committed binaries and source can disagree. If you change
> `shell.c` in this directory, the committed `kernel.bin` / `helloworld-c.img`
> are now stale until you `make` again — QEMU will happily boot the *old* image.
> Run `make clean && make` after any source edit, or boot from a freshly built
> image, to be sure you are running what you just wrote.


## How to build and run

From `helloworld-os-c-v3/`:

```sh
make            # rebuild boot.bin, kernel.bin, and the images from source
make run        # boot in QEMU
make debug      # boot under QEMU with a GDB stub (-s -S)
make clean      # remove build artifacts
```

Or, because the artifacts are committed, boot directly without building:

```sh
qemu-system-x86_64 -drive format=raw,file=helloworld-c.img
```

The build itself is identical in shape to Stage 4 — freestanding `gcc` for the C
modules, `nasm` for the stubs, `ld` with the linker script, then `cat boot.bin
kernel.bin` into the image. See
[building-and-running.md](../guides/building-and-running.md) and
[toolchain-and-build.md](../reference/toolchain-and-build.md).


## What it teaches

- **Trimming to ship.** Removing a working-but-experimental feature to keep the
  command surface small and fully owned.
- **Locking the surface.** A consolidated, documented command set is a deliverable
  in its own right.
- **Reproducible artifacts.** Committing the built image so the boot-able output
  is pinned to a known-good state and runnable with no toolchain — and
  understanding the staleness risk that comes with it.
- That a "release" is a distinct kind of work from adding features.


## Known limits

These are inherited unchanged from Stage 4:

- **The scheduler is a model** — no real context switch, no preemption; tasks are
  cooperative.
- **RTC reads aren't fully synchronized** against the CMOS update flag.
- **`calc` is fixed-point** (three decimals) and overflow-limited.
- **Linear `strcmp` dispatch** over 18 commands.
- **Committed artifacts can drift** from source if edited without rebuilding.


## Where to go next

Stage 5 is the head of the series. To extend it, the natural next steps are the
ones the tutorial deliberately stops short of: an interrupt descriptor table and
a timer to make the scheduler *preemptive* (turning the process model from a
structure into a runtime), a real assembly `context_switch`, and paging. The
[writing-your-own-stage.md](../guides/writing-your-own-stage.md) guide sketches
how to add a stage on top of this one.


## See also

- [Stage 4 — Clock, Processes, and a Calculator](stage-4-clock-processes-calc.md) — the feature set this stage consolidates
- [cooperative-scheduling.md](../concepts/cooperative-scheduling.md) — the scheduler model and what preemption would add
- [command-reference.md](../reference/command-reference.md) — the full 18-command surface
- [toolchain-and-build.md](../reference/toolchain-and-build.md) — how the committed artifacts are produced
- [building-and-running.md](../guides/building-and-running.md) — build or boot the image
- [writing-your-own-stage.md](../guides/writing-your-own-stage.md) — extending the series
- [Home](../Home.md)
