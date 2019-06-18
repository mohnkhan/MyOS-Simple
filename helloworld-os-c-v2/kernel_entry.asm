; ============================================================================
;  kernel_entry.asm  —  32-bit protected-mode entry stub
;
;  The first kernel code the bootloader jumps to (linked at 0x1000 as _start).
;  There is no C runtime, so this stub simply calls the kernel's C entry point
;  (shell_main) and then halts forever if it ever returns.
; ============================================================================
[BITS 32]
[EXTERN shell_main]

global _start

_start:
    call shell_main
    jmp $
