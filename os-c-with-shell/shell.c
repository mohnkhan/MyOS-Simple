// ============================================================================
//  shell.c  —  Interactive command shell (kernel entry: shell_main)
//
//  The kernel for this stage. Runs in 32-bit protected mode, writes directly to
//  the VGA text buffer at 0xB8000, and reads the keyboard by polling the 8042
//  controller (ports 0x60/0x64) — BIOS input is unavailable here. It prints a
//  green "shell> " prompt, supports backspace/shift line editing, and dispatches
//  five built-in commands: help, clear, echo, about, shutdown. There is no
//  libc, so strcmp and the command table are written by hand.
// ============================================================================
#include "keyboard.h"

#define VIDEO_MEMORY 0xb8000
#define MAX_CMD_LEN 128

// VGA colors
#define BLACK        0x0
#define BLUE         0x1
#define GREEN        0x2
#define CYAN         0x3
#define RED          0x4
#define WHITE        0xF
#define YELLOW       0xE

// Color combinations
#define WHITE_ON_BLACK  0x0F
#define GREEN_ON_BLACK  0x02
#define CYAN_ON_BLACK   0x03
#define RED_ON_BLACK    0x04
#define YELLOW_ON_BLACK 0x0E

// Global state
int cursor_x = 0;
int cursor_y = 0;
KeyboardState kbd_state = {0, 0, 0, 0};

// Basic I/O functions
unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void putchar_at(char c, int x, int y, char attr) {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    int offset = (y * 80 + x) * 2;
    video[offset] = c;
    video[offset + 1] = attr;
}

void clear_screen() {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video[i] = ' ';
        video[i + 1] = WHITE_ON_BLACK;
    }
    cursor_x = 0;
    cursor_y = 0;
}

void scroll_screen() {
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

void putchar(char c, char color) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            putchar_at(' ', cursor_x, cursor_y, color);
        }
    } else {
        putchar_at(c, cursor_x, cursor_y, color);
        cursor_x++;
    }
    
    if (cursor_x >= 80) {
        cursor_x = 0;
        cursor_y++;
    }
    
    if (cursor_y >= 25) {
        scroll_screen();
        cursor_y = 24;
    }
}

void print(const char* str, char color) {
    int i = 0;
    while (str[i] != '\0') {
        putchar(str[i], color);
        i++;
    }
}

void println(const char* str, char color) {
    print(str, color);
    putchar('\n', color);
}

// String functions
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, int n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// Keyboard input
char get_key() {
    unsigned char scancode;
    char ascii_char = 0;
    
    // Wait for keyboard data
    while(!(inb(KEYBOARD_STATUS_PORT) & KEYBOARD_OUTPUT_FULL));
    
    // Read scancode
    scancode = inb(KEYBOARD_DATA_PORT);
    
    // Handle key release
    if (scancode & KEY_RELEASE_BIT) {
        scancode &= ~KEY_RELEASE_BIT;
        
        // Update modifier states on release
        if (scancode == SCANCODE_LEFT_SHIFT || scancode == SCANCODE_RIGHT_SHIFT) {
            kbd_state.shift_pressed = 0;
        }
        return 0;
    }
    
    // Handle modifiers
    if (scancode == SCANCODE_LEFT_SHIFT || scancode == SCANCODE_RIGHT_SHIFT) {
        kbd_state.shift_pressed = 1;
        return 0;
    }
    
    // Convert scancode to ASCII
    if (scancode < 90) {
        if (kbd_state.shift_pressed) {
            ascii_char = scancode_to_ascii_shift[scancode];
        } else {
            ascii_char = scancode_to_ascii[scancode];
        }
    }
    
    // Handle special keys
    if (scancode == SCANCODE_ENTER) return '\n';
    if (scancode == SCANCODE_BACKSPACE) return '\b';
    
    return ascii_char;
}

void get_line(char* buffer, int max_len) {
    int pos = 0;
    char key;
    
    while (pos < max_len - 1) {
        key = get_key();
        
        if (key == 0) continue;
        
        if (key == '\n') {
            buffer[pos] = '\0';
            putchar('\n', WHITE_ON_BLACK);
            break;
        } else if (key == '\b') {
            if (pos > 0) {
                pos--;
                putchar('\b', WHITE_ON_BLACK);
            }
        } else if (key >= 32 && key <= 126) {
            buffer[pos++] = key;
            putchar(key, WHITE_ON_BLACK);
        }
    }
    buffer[pos] = '\0';
}

// Extract command and arguments
void parse_command(char* input, char* cmd, char* args) {
    int i = 0, j = 0;
    
    // Skip leading spaces
    while (input[i] == ' ') i++;
    
    // Extract command
    while (input[i] && input[i] != ' ') {
        cmd[j++] = input[i++];
    }
    cmd[j] = '\0';
    
    // Skip spaces
    while (input[i] == ' ') i++;
    
    // Copy rest as arguments
    j = 0;
    while (input[i]) {
        args[j++] = input[i++];
    }
    args[j] = '\0';
}

// Command implementations
void cmd_help() {
    println("Available commands:", CYAN_ON_BLACK);
    println("  help     - Show this help message", WHITE_ON_BLACK);
    println("  clear    - Clear the screen", WHITE_ON_BLACK);
    println("  echo     - Echo text back to screen", WHITE_ON_BLACK);
    println("  about    - About this OS", WHITE_ON_BLACK);
    println("  shutdown - Halt the system", WHITE_ON_BLACK);
}

void cmd_about() {
    println("SimpleShell OS v1.0", YELLOW_ON_BLACK);
    println("A minimal operating system with command interpreter", WHITE_ON_BLACK);
    println("Features:", CYAN_ON_BLACK);
    println("  - Protected mode operation", WHITE_ON_BLACK);
    println("  - VGA text mode display", WHITE_ON_BLACK);
    println("  - Simple command shell", WHITE_ON_BLACK);
}

void cmd_echo(char* args) {
    if (args[0] == '\0') {
        println("", WHITE_ON_BLACK);
    } else {
        println(args, GREEN_ON_BLACK);
    }
}

void shutdown() {
    clear_screen();
    println("System halted.", RED_ON_BLACK);
    __asm__ __volatile__("cli");
    __asm__ __volatile__("hlt");
    while(1);
}

// Main shell loop
void shell_main() {
    char input[MAX_CMD_LEN];
    char cmd[64];
    char args[MAX_CMD_LEN];
    
    clear_screen();
    println("SimpleShell OS v1.0", YELLOW_ON_BLACK);
    println("Type 'help' for available commands", CYAN_ON_BLACK);
    println("", WHITE_ON_BLACK);
    
    while (1) {
        print("shell> ", GREEN_ON_BLACK);
        get_line(input, MAX_CMD_LEN);
        
        parse_command(input, cmd, args);
        
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
    }
}

// Entry point for the kernel - called from kernel_entry.asm
void kernel_main() {
    shell_main();
}
