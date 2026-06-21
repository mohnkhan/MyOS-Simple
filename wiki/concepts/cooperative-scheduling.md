[← Home](../Home.md)

# Cooperative Scheduling

*A static process table, per-task PCBs, and a round-robin ready queue — a faithful model of a scheduler whose register-level context switch is, honestly, not yet wired up.*

[Stage 4](../stages/stage-4-clock-processes-calc.md) introduces a **process subsystem**:
Process Control Blocks, process states, a ready queue, and a cooperative round-robin scheduler,
all in `process.h` and `process.c`. It is an excellent way to *see* how a kernel models tasks.
But it is important to be clear up front about what it does and does not do — and the most
valuable thing this article can teach is exactly where the model stops and a real kernel begins.

> ⚠️ **Caveat — read this first:** The register-level context switch is **modeled, not
> performed**. `process_scheduler()` updates the bookkeeping (which PCB is "current", the order
> of the ready queue) but the line that would actually save and restore CPU registers is a
> comment. So MyOS does not concurrently multitask. The rest of this article explains the model
> accurately and then spells out precisely what is missing — that honesty is the point.

## The Process Control Block

Every task is described by a **PCB**, the `process_t` struct (`process.h:46`):

```c
typedef struct process_t {
    uint32_t pid;                    // Process ID
    char name[PROCESS_NAME_LEN];     // Process name
    process_state_t state;           // Current state
    cpu_context_t context;           // CPU context for context switching
    uint8_t* stack;                  // Stack pointer
    uint32_t stack_size;             // Stack size
    void (*entry_point)(void);       // Process entry point
    uint32_t time_slice;             // Time slice for scheduling
    uint32_t priority;               // Process priority
    struct process_t* next;          // Next process in queue
} process_t;
```

The `cpu_context_t` it embeds is where a task's registers *would* be parked while another task
runs (`process.h:26`): the general registers `eax`–`edx`, the index registers `esi`/`edi`, the
stack frame pointers `ebp`/`esp`, the instruction pointer `eip`, `eflags`, and the segment
selectors `cs`/`ds`/`es`/`fs`/`gs`/`ss`. That is, in principle, everything you would need to
freeze and thaw a thread of execution.

A process moves through four states (`process.h:18`):

```c
typedef enum {
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
} process_state_t;
```

## Everything is static

There is no heap in MyOS, so there is no dynamic allocation. The process table and every stack
are fixed-size static arrays (`process.c:16`, `process.c:23`):

```c
#define MAX_PROCESSES 10
#define STACK_SIZE 4096
#define PROCESS_NAME_LEN 32

static process_t process_table[MAX_PROCESSES];
static uint8_t process_stacks[MAX_PROCESSES][STACK_SIZE] __attribute__((aligned(16)));
```

Stacks are 16-byte aligned (the x86 ABI alignment for stack frames), and `allocate_stack()` is
just a linear scan of a `stack_allocation[]` bitmap-of-sorts — claim the first free slot, return
its 4 KiB block (`process.c:27`). "Freeing" a stack clears the flag. Ten tasks, ten stacks, no
fragmentation, no `malloc`.

> 💡 **Tidbit:** Static everything is a deliberate constraint of freestanding kernel code, not
> laziness. With no allocator yet written, the process table *is* the memory manager for tasks.
> Many real microkernels keep their PCB table static too, precisely so the scheduler can never
> fail for lack of memory at a moment when it has nothing to fall back on.

## Creating a process

`process_init()` runs first and creates the **idle process, PID 0** — the task that "runs" when
nothing else is ready (`process.c:57`). The `next_pid` counter starts at 1, so every real task
gets PID 1, 2, 3, … (`process.c:19`).

`process_create()` finds a free slot, allocates a stack, fills in the PCB, and seeds a fresh CPU
context (`process.c:81`):

```c
proc->time_slice = 5 - priority;             // higher priority -> bigger slice

proc->context.esp = (uint32_t)(stack + STACK_SIZE - 4);  // top of stack, minus a word
proc->context.ebp = proc->context.esp;
proc->context.eip = (uint32_t)entry_point;   // where the task starts executing
proc->context.eflags = 0x200;                // IF set: interrupts enabled
```

Two details are worth unpacking:

- **Time slice as `5 - priority`.** A higher `priority` value yields a *larger* slice, so
  priority 0 gets 5 ticks and priority 4 gets 1. This is recorded in the PCB but, because
  scheduling here is cooperative, the slice is informational — nothing preempts a task when its
  slice expires.
- **`eflags = 0x200`.** That single bit is **IF**, the interrupt flag, at bit 9
  (`0x200 == 1 << 9`). Seeding it set means the task would begin life with interrupts enabled —
  the correct default for any task that should be interruptible.

Newly created processes are appended to the tail of the singly-linked **ready queue**, threaded
through each PCB's `->next` pointer (`process.c:127`).

> 💡 **Tidbit:** `eflags = 0x200` is the *only* flag set in a fresh context — IF and nothing
> else. It is the same value the CMOS RTC code never touches but that a real timer-driven
> scheduler would depend on, because preemption *is* a timer interrupt firing while IF is set.

## The round-robin scheduler

The whole scheduling policy lives in `process_scheduler()`. It rotates the current task to the
back of the queue and picks the new head (`process.c:205`):

```c
void process_scheduler(void) {
    if (!ready_queue) {
        return;
    }

    process_t* next = ready_queue;

    // Move current to end of queue if still ready
    if (current_process && current_process->state == PROCESS_READY) {
        ready_queue = ready_queue->next;          // pop from front
        process_t* p = ready_queue;
        if (p) {
            while (p->next) p = p->next;           // walk to tail
            p->next = current_process;             // append
            current_process->next = NULL;
        } else {
            ready_queue = current_process;
        }
    }

    // Select next process
    next = ready_queue;
    if (next && next != current_process) {
        process_t* old = current_process;
        current_process = next;
        current_process->state = PROCESS_RUNNING;

        // Context switch would happen here in real implementation
        // context_switch(&old->context, &current->context);
    }
}
```

That is textbook round-robin: pop the head, append it to the tail, run whoever is now at the
head, repeat. Scheduling is **cooperative** — it only happens when a task voluntarily calls
`process_yield()`, which is a one-line wrapper around the scheduler (`process.c:243`):

```c
void process_yield(void) {
    process_scheduler();
}
```

## Where the model stops: the context switch that isn't

Look again at the last two lines of `process_scheduler()`:

```c
        // Context switch would happen here in real implementation
        // context_switch(&old->context, &current->context);
```

Both are **comments**. The scheduler updates `current_process` and reorders the queue, but it
never saves the outgoing task's registers into `old->context`, and it never loads the incoming
task's registers from `current->context`. The function that would do this is *declared* but
never defined or called (`process.h:73`):

```c
// Context switching (assembly)
extern void context_switch(cpu_context_t* old_context, cpu_context_t* new_context);
```

`extern` with no implementation anywhere means there is no machine code behind it. Because of
this, the carefully-seeded `esp`/`ebp`/`eip`/`eflags` in each PCB are never actually loaded into
the CPU. The subsystem is an **illustrative model** of a PCB and a scheduler, not a working
multitasking kernel.

### So what actually runs?

The three sample workloads each contain their own infinite loop with a busy-wait and a yield
(`process.c:330`):

```c
void process_counter(void) {
    int count = 0;
    while (1) {
        print_string("[Counter] Count: ");
        print_int(count++);
        print_string("\n");
        for (volatile int i = 0; i < 10000000; i++);  // simulate work
        process_yield();                               // cooperate
    }
}
```

`process_fibonacci()` and `process_prime()` follow the same shape (`process.c:346`,
`process.c:366`). When one of these runs, it runs *its own* loop to completion (which, being
`while(1)`, never returns). The call to `process_yield()` faithfully reorders the queue and
re-points `current_process` — so `ps`-style listings change — but execution does not jump into a
different saved register set, because nothing restores one. There is no true concurrent switching
between tasks.

### What a real implementation would add

To turn this model into genuine multitasking you would need two things MyOS does not yet have:

1. **An assembly context-switch routine.** A C function cannot portably save and restore `esp`,
   `eip`, and the rest. You write `context_switch` in assembly: push the live registers, store
   `esp` into `old_context`, load `esp` from `new_context`, pop the registers, and `ret` — which
   resumes the new task exactly where *it* last called `context_switch`. This is the body that
   the `extern` declaration is promising.
2. **A timer IRQ for preemption.** Cooperative scheduling only changes tasks when one yields.
   For *preemptive* multitasking you program the PIT/APIC timer to fire periodically; its
   interrupt handler calls the scheduler and the context switch, forcing a task switch whether
   or not the running task cooperated. This is where the per-PCB `time_slice` would finally
   matter, and where the `eflags = 0x200` (IF set) seeding pays off.

> ⚠️ **Caveat:** Cooperative scheduling has a famous failure mode that applies even to a working
> implementation: **one task that never yields hangs the entire system.** There is no timer to
> wrest control back. This is exactly the trade-off that made classic Mac OS (through version 9)
> and Windows 3.x so fragile — a single misbehaving program could freeze the whole machine.
> Preemptive scheduling, backed by a timer interrupt, is what fixed it.

## Lifecycle helpers

The subsystem rounds out the model with the operations you would expect, all manipulating the
PCB table and ready queue:

- `process_terminate()` / `process_kill()` — unlink from the queue, free the stack, mark
  `TERMINATED` (`process.c:149`).
- `process_suspend()` — set state to `BLOCKED` (`process.c:291`).
- `process_resume()` — flip back to `READY` and re-append to the queue (`process.c:306`).
- `process_list()` — print the PID/name/state/priority table (`process.c:248`).

These are exposed through the shell; see the
[command reference](../reference/command-reference.md) for the user-facing process commands and
the [Stage 4 walkthrough](../stages/stage-4-clock-processes-calc.md) for how the subsystem fits
alongside the clock and calculator.

## See also

- [Stage 4: clock, processes, calculator](../stages/stage-4-clock-processes-calc.md) — where the process model is introduced
- [Command reference](../reference/command-reference.md) — the process-management shell commands
- [CMOS RTC](cmos-rtc.md) — the clock half of Stage 4
- [Fixed-point arithmetic](fixed-point-arithmetic.md) — the calculator half of Stage 4
- [Protected mode](protected-mode.md) — the 32-bit environment these tasks run in
- [Memory map](../reference/memory-map.md) — where the static process table and stacks live
- [Glossary](../reference/glossary.md) — PCB, context switch, preemption
- [Home](../Home.md)
