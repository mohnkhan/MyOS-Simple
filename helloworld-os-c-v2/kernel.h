// ============================================================================
//  kernel.h  —  Shared VGA text-output helpers (header-only)
//
//  Inline routines used across the kernel for writing to the 80x25 VGA text
//  buffer at 0xB8000: place a character+attribute at a cell, scroll the screen,
//  handle newline/backspace/wrap, and print strings and integers. cursor_x and
//  cursor_y are defined in shell.c and shared here via extern.
// ============================================================================
#ifndef KERNEL_H
#define KERNEL_H

// VGA text mode memory address
#define VIDEO_MEMORY 0xb8000

// VGA colors
#define WHITE_ON_BLACK  0x0F
#define GREEN_ON_BLACK  0x02
#define CYAN_ON_BLACK   0x03
#define RED_ON_BLACK    0x04
#define YELLOW_ON_BLACK 0x0E

// External declarations from shell.c
extern int cursor_x;
extern int cursor_y;

// Implementation of kernel I/O functions
static inline void putchar_at_kernel(char c, int x, int y, char attr) {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    int offset = (y * 80 + x) * 2;
    video[offset] = c;
    video[offset + 1] = attr;
}

static inline void scroll_screen_kernel() {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    
    // Move all lines up by one
    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 80; x++) {
            int src_offset = ((y + 1) * 80 + x) * 2;
            int dst_offset = (y * 80 + x) * 2;
            video[dst_offset] = video[src_offset];
            video[dst_offset + 1] = video[src_offset + 1];
        }
    }
    
    // Clear the last line
    for (int x = 0; x < 80; x++) {
        int offset = (24 * 80 + x) * 2;
        video[offset] = ' ';
        video[offset + 1] = WHITE_ON_BLACK;
    }
}

static inline void print_char_impl(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            putchar_at_kernel(' ', cursor_x, cursor_y, WHITE_ON_BLACK);
        }
    } else {
        putchar_at_kernel(c, cursor_x, cursor_y, WHITE_ON_BLACK);
        cursor_x++;
    }
    
    if (cursor_x >= 80) {
        cursor_x = 0;
        cursor_y++;
    }
    
    if (cursor_y >= 25) {
        scroll_screen_kernel();
        cursor_y = 24;
    }
}

static inline void print_string(const char* str) {
    int i = 0;
    while (str[i] != '\0') {
        print_char_impl(str[i]);
        i++;
    }
}

static inline void print_int(int num) {
    char str[12];  // Enough for 32-bit int
    int i = 0;
    int is_negative = 0;
    
    if (num == 0) {
        print_char_impl('0');
        return;
    }
    
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }
    
    // Convert to string (reversed)
    while (num > 0) {
        str[i++] = (num % 10) + '0';
        num /= 10;
    }
    
    if (is_negative) {
        print_char_impl('-');
    }
    
    // Print in correct order
    while (i > 0) {
        print_char_impl(str[--i]);
    }
}

// Define print_char using the implementation
#define print_char(c) print_char_impl(c)

#endif // KERNEL_H
