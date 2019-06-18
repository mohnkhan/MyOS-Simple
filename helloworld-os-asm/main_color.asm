; ============================================================================
;  main_color.asm  —  Hello World OS, color + interactive (boot sector)
;
;  An enhanced single-sector OS in 16-bit real mode. Paints a blue background,
;  draws a colored title/message/signature/help line via BIOS int 0x10 (AH=0x09
;  write char+attribute, AH=0x02 set cursor, AH=0x06 clear window), then loops
;  on int 0x16: keys 1-5 change the background, SPACE resets, Q quits. A VGA
;  attribute byte is (background << 4) | foreground. Ends in the 0xAA55 boot
;  signature.
; ============================================================================
[BITS 16]
[ORG 0x7C00]

; Color attributes (background | foreground)
%define WHITE_ON_BLUE      0x1F
%define YELLOW_ON_BLACK    0x0E
%define CYAN_ON_BLACK      0x03
%define GREEN_ON_BLACK     0x02
%define RED_ON_BLACK       0x04
%define BRIGHT_WHITE       0x0F

start:
    ; Set video mode
    mov ax, 0x0003
    int 0x10
    
    ; Clear screen with blue background
    mov ah, 0x06
    mov al, 0
    mov bh, WHITE_ON_BLUE
    mov cx, 0x0000
    mov dx, 0x184F
    int 0x10
    
    ; Print title
    mov dh, 10
    mov dl, 25
    mov bl, YELLOW_ON_BLACK
    mov si, title
    call print_color
    
    ; Print message
    mov dh, 12
    mov dl, 34
    mov bl, BRIGHT_WHITE
    mov si, msg
    call print_color
    
    ; Print signature
    mov dh, 14
    mov dl, 31
    mov bl, CYAN_ON_BLACK
    mov si, sign
    call print_color
    
    ; Print instructions
    mov dh, 17
    mov dl, 20
    mov bl, GREEN_ON_BLACK
    mov si, help
    call print_color

keyboard_loop:
    ; Wait for key
    mov ah, 0x00
    int 0x16
    
    ; Check quit
    cmp al, 'q'
    je quit
    cmp al, 'Q'
    je quit
    
    ; Check for 1-5 (background colors)
    cmp al, '1'
    jb keyboard_loop
    cmp al, '5'
    ja check_special
    
    ; Change background
    sub al, '0'
    shl al, 4
    or al, 0x0F
    mov bh, al
    mov ah, 0x06
    mov al, 0
    mov cx, 0x0000
    mov dx, 0x184F
    int 0x10
    jmp redraw
    
check_special:
    ; Space - reset
    cmp al, ' '
    jne keyboard_loop
    mov bh, WHITE_ON_BLUE
    mov ah, 0x06
    mov al, 0
    mov cx, 0x0000
    mov dx, 0x184F
    int 0x10
    
redraw:
    ; Redraw text
    mov dh, 10
    mov dl, 25
    mov bl, YELLOW_ON_BLACK
    mov si, title
    call print_color
    
    mov dh, 12
    mov dl, 34
    mov bl, BRIGHT_WHITE
    mov si, msg
    call print_color
    
    mov dh, 14
    mov dl, 31
    mov bl, CYAN_ON_BLACK
    mov si, sign
    call print_color
    
    mov dh, 17
    mov dl, 20
    mov bl, GREEN_ON_BLACK
    mov si, help
    call print_color
    
    jmp keyboard_loop

quit:
    ; Clear and show goodbye
    mov ax, 0x0003
    int 0x10
    
    mov dh, 12
    mov dl, 36
    mov bl, RED_ON_BLACK
    mov si, bye
    call print_color
    
    ; Halt
    cli
    hlt
    jmp $

; Print string with color
; DH=row, DL=col, BL=color, SI=string
print_color:
    push ax
    push bx
    push cx
    push dx
    
    ; Set cursor
    mov ah, 0x02
    mov bh, 0
    int 0x10
    
.loop:
    lodsb
    or al, al
    jz .done
    
    ; Print char with color
    mov ah, 0x09
    mov bh, 0
    mov cx, 1
    int 0x10
    
    ; Move cursor
    inc dl
    mov ah, 0x02
    int 0x10
    jmp .loop
    
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Data
title db "COLORFUL HELLO WORLD OS", 0
msg db "Hello, World!", 0
sign db "Booted successfully", 0
help db "1-5: Colors | SPACE: Reset | Q: Quit", 0
bye db "Goodbye!", 0

; Boot sector padding
times 510 - ($ - $$) db 0
dw 0xAA55
