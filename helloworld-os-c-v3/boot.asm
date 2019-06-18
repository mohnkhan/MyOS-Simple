; ============================================================================
;  boot.asm  —  Bootloader: real mode -> 32-bit protected mode
;
;  Loaded by the BIOS at 0x7C00. Sets up segments and a stack, reads the
;  kernel from disk to 0x1000 via BIOS int 0x13 (CHS, starting at sector 2),
;  installs a flat 3-entry GDT (null / 4 GiB ring-0 code / 4 GiB ring-0 data),
;  sets CR0.PE, and far-jumps to flush the pipeline into 32-bit code. In
;  protected mode it reloads the data selectors, sets the stack to 0x90000,
;  and calls the kernel at 0x1000. Loads 39 sectors (~19.5 KiB) to cover the
;  shell + RTC + process modules. Ends with the 0xAA55 boot signature.
; ============================================================================
[BITS 16]
[ORG 0x7C00]

KERNEL_OFFSET equ 0x1000

start:
    ; Save boot drive number
    mov [BOOT_DRIVE], dl
    
    ; Set up segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    
    ; Clear screen
    mov ah, 0x00
    mov al, 0x03
    int 0x10
    
    ; Load kernel from disk
    mov bx, KERNEL_OFFSET
    mov dh, 39          ; Load 39 sectors (enough for ~19.5KB kernel with RTC support)
    mov dl, [BOOT_DRIVE]
    call disk_load
    
    ; Switch to protected mode
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEG:init_pm

[BITS 32]
init_pm:
    ; Set up protected mode segments
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    mov ebp, 0x90000
    mov esp, ebp
    
    ; Call C kernel
    call KERNEL_OFFSET
    
    jmp $

[BITS 16]
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
    
disk_error:
    mov si, DISK_ERROR_MSG
    call print_16
    jmp $
    
sectors_error:
    mov si, SECTORS_ERROR_MSG
    call print_16
    jmp $

print_16:
    pusha
    mov ah, 0x0e
.loop:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

; GDT
gdt_start:
    dd 0x0
    dd 0x0

gdt_code:
    dw 0xffff       ; Limit
    dw 0x0          ; Base
    db 0x0          ; Base
    db 10011010b    ; Access
    db 11001111b    ; Flags
    db 0x0          ; Base

gdt_data:
    dw 0xffff
    dw 0x0
    db 0x0
    db 10010010b
    db 11001111b
    db 0x0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

BOOT_DRIVE db 0
DISK_ERROR_MSG db "Disk read error!", 0
SECTORS_ERROR_MSG db "Incorrect sectors read!", 0

times 510-($-$$) db 0
dw 0xAA55
