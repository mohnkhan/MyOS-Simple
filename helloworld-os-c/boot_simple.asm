; ============================================================================
;  boot_simple.asm  —  Self-contained "simple" boot image (16-bit real mode)
;
;  A complete program that lives entirely in the 512-byte boot sector. Unlike
;  boot.asm it loads no kernel: the BIOS maps it to 0x7C00 and it runs start to
;  finish in real mode using only BIOS interrupts (int 0x10 video, int 0x16
;  keyboard). It clears the screen to a blue background, prints a colored
;  banner, waits for Q, then halts. Built directly as helloworld-simple.img so
;  a stage can be booted without the protected-mode toolchain.
; ============================================================================
[BITS 16]
[ORG 0x7C00]

; Color definitions (background | foreground)
BLACK_ON_WHITE equ 0x70
YELLOW_ON_BLUE equ 0x1E
GREEN_ON_BLACK equ 0x02
RED_ON_BLACK equ 0x04
CYAN_ON_BLACK equ 0x03
MAGENTA_ON_BLACK equ 0x05
WHITE_ON_BLUE equ 0x1F

start:
    ; Set up segments and stack
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    
    ; Set video mode with blue background
    mov ah, 0x06    ; Scroll up function
    mov al, 0x00    ; Clear entire screen
    mov bh, WHITE_ON_BLUE  ; Attribute for blank lines
    mov cx, 0x0000  ; Upper left corner (0,0)
    mov dx, 0x184F  ; Lower right corner (24,79)
    int 0x10
    
    ; Reset cursor to top
    mov ah, 0x02
    mov bh, 0
    mov dx, 0x0000
    int 0x10
    
    ; Print colorful title at top
    mov ah, 0x02
    mov bh, 0
    mov dh, 5       ; Row 5
    mov dl, 25      ; Column 25
    int 0x10
    
    mov bl, YELLOW_ON_BLUE
    mov si, title_msg
    call print_string_color
    
    ; Print main message with different color
    mov ah, 0x02
    mov bh, 0
    mov dh, 12      ; Row 12
    mov dl, 34      ; Column 34
    int 0x10
    
    mov bl, BLACK_ON_WHITE
    mov si, hello_msg
    call print_string_color
    
    ; Print second message in green
    mov ah, 0x02
    mov bh, 0
    mov dh, 14      ; Row 14
    mov dl, 31      ; Column 31
    int 0x10
    
    mov bl, GREEN_ON_BLACK
    mov si, boot_msg
    call print_string_color
    
    ; Print instruction in cyan
    mov ah, 0x02
    mov bh, 0
    mov dh, 20      ; Row 20
    mov dl, 32      ; Column 32
    int 0x10
    
    mov bl, CYAN_ON_BLACK
    mov si, quit_msg
    call print_string_color

wait_key:
    mov ah, 0x00
    int 0x16        ; Wait for keypress
    
    cmp al, 'q'
    je shutdown
    cmp al, 'Q'
    je shutdown
    jmp wait_key

shutdown:
    ; Clear screen with red background before shutdown
    mov ah, 0x06
    mov al, 0x00
    mov bh, 0x40    ; Red background, black text
    mov cx, 0x0000
    mov dx, 0x184F
    int 0x10
    
    ; Print shutdown message in white on red
    mov ah, 0x02
    mov bh, 0
    mov dh, 12
    mov dl, 32
    int 0x10
    
    mov bl, 0x4F    ; White on red
    mov si, shutdown_msg
    call print_string_color
    
    ; Small delay
    mov cx, 0xFFFF
.delay:
    loop .delay
    
    cli
    hlt

print_string:
    pusha
    mov ah, 0x0E
.loop:
    lodsb
    or al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

print_string_color:
    pusha
    mov ah, 0x09    ; Write character with attribute
    mov bh, 0       ; Page 0
    mov cx, 1       ; Write 1 character
.loop:
    lodsb
    or al, al
    jz .done
    int 0x10
    ; Move cursor forward
    push ax
    mov ah, 0x03    ; Get cursor position
    int 0x10
    inc dl          ; Move cursor right
    mov ah, 0x02    ; Set cursor position
    int 0x10
    pop ax
    mov ah, 0x09    ; Restore write function
    jmp .loop
.done:
    popa
    ret

title_msg db "== Colorful Hello World OS ==", 0
hello_msg db "Hello, World!", 0
boot_msg db "Booted successfully", 0
quit_msg db "Press Q to exit", 0
shutdown_msg db "Shutting down...", 0

times 510-($-$$) db 0
dw 0xAA55
