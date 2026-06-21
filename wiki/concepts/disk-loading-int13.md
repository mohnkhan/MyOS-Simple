# Disk Loading with INT 13h

*How the 512-byte bootloader reaches past its own size limit and pulls a full kernel off disk — CHS addressing, sector counts, and the carry-flag error path.*

A [boot sector](boot-sector.md) is only 510 usable bytes — far too small for a C
kernel. So from [stage 2](../stages/stage-2-c-protected-mode.md) onward, the boot
sector becomes a **bootloader** whose central job is to read the rest of the
kernel from disk into memory before switching to
[protected mode](protected-mode.md). It does this with the BIOS disk service,
`int 0x13`, function `AH = 0x02` (read sectors). This page dissects exactly how
MyOS-Simple's `disk_load` routine works.

## The call site

The bootloader sets up three things and calls `disk_load`:

```asm
    ; Load kernel from disk
    mov bx, KERNEL_OFFSET
    mov dh, 16          ; Load 16 sectors (kernel is ~3.6KB, needs >2)
    mov dl, [BOOT_DRIVE]
    call disk_load
```

`helloworld-os-c/boot.asm:34-37`

- `BX = KERNEL_OFFSET` (`0x1000`) — the **destination offset** in memory.
- `DH = 16` — the **number of sectors** to read (this value is the only thing
  that changes between stages).
- `DL = [BOOT_DRIVE]` — the **drive number** the BIOS gave us in `DL` at boot,
  which the bootloader saved on its very first instruction
  (`helloworld-os-c/boot.asm:19`).

`KERNEL_OFFSET` is defined as `0x1000` at the top of the file
(`helloworld-os-c/boot.asm:15`) — the same address the
[linker script](linker-scripts.md) links the kernel to, and the same address the
bootloader later `call`s to enter the kernel.

## The `disk_load` routine

```asm
disk_load:
    pusha
    push dx

    mov ah, 0x02        ; BIOS read sectors
    mov al, dh          ; Number of sectors
    mov cl, 0x02        ; Start from sector 2
    mov ch, 0x00        ; Cylinder 0
    mov dh, 0x00        ; Head 0

    int 0x13
    jc disk_error

    pop dx
    popa
    ret
```

`helloworld-os-c/boot.asm:66-81`

Walking the registers that `int 0x13` / `AH = 0x02` expects:

| Register | Value | Meaning |
|----------|-------|---------|
| `AH` | `0x02` | Function: read sectors |
| `AL` | `DH` from caller (e.g. 16) | Number of sectors to read |
| `CL` | `0x02` | Starting **sector** number (1-based) |
| `CH` | `0x00` | **Cylinder** number |
| `DH` | `0x00` | **Head** number |
| `DL` | boot drive | Which drive to read from |
| `ES:BX` | `0x0000:0x1000` | Destination address in memory |

Note the `mov al, dh` on entry: the caller passes the sector *count* in `DH`,
and the routine copies it into `AL` where `int 0x13` wants it. Then `DH` is
immediately reused as the head number (`mov dh, 0x00`). The `push dx`/`pop dx`
around the call preserves the caller's drive number in `DL` across the BIOS call.

`ES` was set to 0 back in the segment setup (`boot.asm:23`), and `BX` holds
`0x1000`, so the destination `ES:BX` resolves to physical `0x1000`.

## CHS addressing and the 1-based sector quirk

The BIOS read uses **CHS** geometry — Cylinder, Head, Sector — the
three-coordinate scheme inherited from physical spinning disks. The single most
important detail:

> 💡 **Tidbit:** CHS **sectors are 1-based**, not 0-based. Sector *1* is the very
> first sector on the track — and that is the [boot sector](boot-sector.md)
> itself. The kernel therefore begins at **sector 2**, which is exactly why
> MyOS-Simple's loader sets `CL = 0x02`. (Cylinders and heads, confusingly, *are*
> 0-based — hence `CH = 0` and `DH = 0`.)

So the read says: "starting at cylinder 0, head 0, sector 2, read `AL` sectors
into `ES:BX`." Because the kernel is laid out contiguously right after the boot
sector, sectors 2, 3, 4, … hold the kernel image in order, and a single `int
0x13` call slurps them all into memory at `0x1000`.

## How many sectors to read?

The sector count (`DH` at the call site) is the one number that changes from
stage to stage, because the kernel grows:

| Stage | Project directory | Sectors read | Source |
|-------|-------------------|-------------|--------|
| 2 | `helloworld-os-c` | **16** | `helloworld-os-c/boot.asm:35` |
| 3 | `os-c-with-shell` | **15** | `os-c-with-shell/boot.asm:34` |
| 4 | `helloworld-os-c-v2` | **39** | `helloworld-os-c-v2/boot.asm:35` |
| 5 | `helloworld-os-c-v3` | **39** | `helloworld-os-c-v3/boot.asm:35` |

The guiding principle is "**read more than the minimum**". Each comment in the
source spells out the reasoning — e.g. stage 2's `; Load 16 sectors (kernel is
~3.6KB, needs >2)`. A 3.6 KB kernel only strictly needs 8 sectors
(3600 ÷ 512 ≈ 7.0, rounded up), but reading 16 leaves comfortable headroom so a
slightly larger build is not silently **truncated**.

> ⚠️ **Caveat:** If the loader reads *too few* sectors, the tail of the kernel
> never makes it into memory. The code that was cut off is simply absent — the
> kernel will jump into whatever garbage happens to sit at those addresses and
> crash or hang, often far from the real cause. When a kernel grows past its
> sector budget, bumping the count in `boot.asm` is the fix. This is exactly why
> the count jumps to 39 once the stage 4/5 kernels add the
> [RTC clock](cmos-rtc.md) and other features.

> ⚠️ **Caveat:** This scheme assumes the kernel occupies **contiguous** sectors
> starting at sector 2. MyOS-Simple has no filesystem — the build process simply
> concatenates the boot sector and the kernel binary into one flat disk image, so
> the kernel really is the bytes immediately following the boot sector. There is
> no directory to consult; "where is the kernel?" is answered by raw geometry.

## The error path

If the BIOS cannot complete the read, it sets the **carry flag**. The routine
checks it immediately:

```asm
    int 0x13
    jc disk_error
```

`helloworld-os-c/boot.asm:76-77`

```asm
disk_error:
    mov si, DISK_ERROR_MSG
    call print_16
    jmp $
```

`helloworld-os-c/boot.asm:83-86`

On failure the loader prints `"Disk read error!"` (via the same
[teletype `int 0x10` AH=0x0E loop](bios-services.md) the rest of the boot code
uses) and then `jmp $` — an infinite self-loop that hangs the machine. There is
no retry and no recovery: at this point in the boot there is nothing else the
code *can* do.

> 💡 **Tidbit:** `jc` ("jump if carry") is the idiomatic BIOS error check. Almost
> every `int 0x13` function reports failure by setting the carry flag (and an
> error code in `AH`), so `int 0x13` followed by `jc <error>` is a pattern you
> will see in every bootloader ever written.

The source also defines a `sectors_error` handler and a `"Incorrect sectors
read!"` message (`helloworld-os-c/boot.asm:88-91, 138`), intended for verifying
that `AL` (the BIOS's count of sectors actually read) matches the requested
count. In MyOS-Simple's loader that check is not wired into the read path — only
the carry-flag `disk_error` branch is reached — but the scaffolding is present
for anyone extending the loader to verify the returned count.

## What happens next

Once the kernel is safely at `0x1000`, the bootloader continues:
`cli`, `lgdt`, set `CR0.PE`, far-jump into
[protected mode](protected-mode.md), reload the data selectors, set the stack to
`0x90000`, and `call 0x1000` to enter the kernel. That hand-off is the subject of
the [boot process](boot-process.md) spine and the
[Global Descriptor Table](global-descriptor-table.md) page.

## See also

- [The boot process](boot-process.md) — the full boot timeline this read sits in
- [The boot sector](boot-sector.md) — why the kernel can't fit in sector 1
- [BIOS services](bios-services.md) — `int 0x13` alongside `int 0x10` and `int 0x16`
- [Real mode](real-mode.md) — the environment in which `int 0x13` is callable
- [Linker scripts](linker-scripts.md) — why the kernel is linked at `0x1000`
- [Protected mode](protected-mode.md) — where the boot hands off after the load
- [Memory map](../reference/memory-map.md) — `0x1000` kernel load address and more
- [Stage 2: C in protected mode](../stages/stage-2-c-protected-mode.md) · [Stage 4: clock, processes, calc](../stages/stage-4-clock-processes-calc.md)
- [Home](../Home.md)
