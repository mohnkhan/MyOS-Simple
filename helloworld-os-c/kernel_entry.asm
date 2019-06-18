; ============================================================================
;  kernel_entry.asm  —  32-bit protected-mode entry stub
;
;  The first kernel code the bootloader jumps to (linked at 0x1000 as _start).
;  With no C runtime present, it simply calls the C entry point (kernel_main)
;  and halts forever if it ever returns.
; ============================================================================
[BITS 32]
[EXTERN kernel_main]

global _start

_start:
    call kernel_main
    jmp $
