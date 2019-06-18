// ============================================================================
//  kernel.c  —  VGA text + keyboard demo kernel (reference copy)
//
//  The original protected-mode C kernel: defines the 16 VGA colors, packs
//  attribute bytes, writes characters directly to video memory at 0xB8000, and
//  reads the keyboard via scancodes. Retained here for reference — in the stages
//  that ship this file the shell (shell.c) is the kernel that is actually
//  compiled and linked, and this file is not part of the build.
// ============================================================================
#include "keyboard.h"

#define VIDEO_MEMORY 0xb8000

// VGA color constants (background << 4 | foreground)
#define BLACK        0x0
#define BLUE         0x1
#define GREEN        0x2
#define CYAN         0x3
#define RED          0x4
#define MAGENTA      0x5
#define BROWN        0x6
#define LIGHT_GRAY   0x7
#define DARK_GRAY    0x8
#define LIGHT_BLUE   0x9
#define LIGHT_GREEN  0xA
#define LIGHT_CYAN   0xB
#define LIGHT_RED    0xC
#define LIGHT_MAGENTA 0xD
#define YELLOW       0xE
#define WHITE        0xF

// Color combinations
#define WHITE_ON_BLACK  0x0F
#define BLACK_ON_WHITE  0x70
#define GREEN_ON_BLACK  0x02
#define CYAN_ON_BLACK   0x03
#define RED_ON_BLACK    0x04
#define YELLOW_ON_BLACK 0x0E
#define WHITE_ON_BLUE   0x1F

// Global state
int cursor_x = 0;
int cursor_y = 0;
KeyboardState kbd_state = {0, 0, 0, 0};
char current_color = WHITE_ON_BLACK;

// Function to create color attribute
char make_color(char bg, char fg) {
    return (bg << 4) | fg;
}

// Function to write a character at specific position with color
void putchar_at(char c, int x, int y, char attr) {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    int offset = (y * 80 + x) * 2;
    video[offset] = c;
    video[offset + 1] = attr;
}

// Clear the screen with specific background color
void clear_screen_color(char bg_color) {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    char attr = make_color(bg_color, WHITE);
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video[i] = ' ';
        video[i + 1] = attr;
    }
}

// Clear the screen (default black background)
void clear_screen() {
    clear_screen_color(BLACK);
    cursor_x = 0;
    cursor_y = 0;
}

// Scroll the screen up by one line
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

// Print a character with automatic scrolling
void putchar(char c, char color) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            putchar_at(' ', cursor_x, cursor_y, color);
        }
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;  // Align to next tab stop
    } else {
        putchar_at(c, cursor_x, cursor_y, color);
        cursor_x++;
    }
    
    // Handle line wrap
    if (cursor_x >= 80) {
        cursor_x = 0;
        cursor_y++;
    }
    
    // Handle scrolling
    if (cursor_y >= 25) {
        scroll_screen();
        cursor_y = 24;
    }
}

// Print a string
void print(const char* str, char color) {
    int i = 0;
    while (str[i] != '\0') {
        putchar(str[i], color);
        i++;
    }
}

// Print a string with newline
void println(const char* str, char color) {
    print(str, color);
    putchar('\n', color);
}

// String comparison
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// String comparison up to n characters
int strncmp(const char* s1, const char* s2, int n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// String copy
void strcpy(char* dest, const char* src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

// String length
int strlen(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

// Read keyboard port
unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

// Advanced keyboard input handling
char get_key_advanced() {
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
        } else if (scancode == SCANCODE_LEFT_CTRL) {
            kbd_state.ctrl_pressed = 0;
        } else if (scancode == SCANCODE_LEFT_ALT) {
            kbd_state.alt_pressed = 0;
        }
        return 0; // Return 0 for key releases
    }
    
    // Handle key press
    // Update modifier states
    if (scancode == SCANCODE_LEFT_SHIFT || scancode == SCANCODE_RIGHT_SHIFT) {
        kbd_state.shift_pressed = 1;
        return 0;
    } else if (scancode == SCANCODE_LEFT_CTRL) {
        kbd_state.ctrl_pressed = 1;
        return 0;
    } else if (scancode == SCANCODE_LEFT_ALT) {
        kbd_state.alt_pressed = 1;
        return 0;
    } else if (scancode == SCANCODE_CAPS_LOCK) {
        kbd_state.caps_lock = !kbd_state.caps_lock;
        return 0;
    }
    
    // Convert scancode to ASCII
    if (scancode < 90) {
        if (kbd_state.shift_pressed || kbd_state.caps_lock) {
            ascii_char = scancode_to_ascii_shift[scancode];
        } else {
            ascii_char = scancode_to_ascii[scancode];
        }
        
        // Handle caps lock for letters
        if (kbd_state.caps_lock && !kbd_state.shift_pressed) {
            if (ascii_char >= 'a' && ascii_char <= 'z') {
                ascii_char -= 32; // Convert to uppercase
            }
        } else if (kbd_state.caps_lock && kbd_state.shift_pressed) {
            if (ascii_char >= 'A' && ascii_char <= 'Z') {
                ascii_char += 32; // Convert to lowercase
            }
        }
    }
    
    // Handle special keys
    if (scancode == SCANCODE_UP) return 0x11;    // Up arrow
    if (scancode == SCANCODE_DOWN) return 0x12;  // Down arrow
    if (scancode == SCANCODE_LEFT) return 0x13;  // Left arrow
    if (scancode == SCANCODE_RIGHT) return 0x14; // Right arrow
    if (scancode == SCANCODE_ESC) return 27;
    if (scancode == SCANCODE_ENTER) return '\n';
    if (scancode == SCANCODE_BACKSPACE) return '\b';
    if (scancode == SCANCODE_TAB) return '\t';
    
    return ascii_char;
}

// Get a line of input from the user
void get_line(char* buffer, int max_len) {
    int pos = 0;
    char key;
    
    while (pos < max_len - 1) {
        key = get_key_advanced();
        
        if (key == 0) continue;  // Skip key releases and modifiers
        
        if (key == '\n') {
            buffer[pos] = '\0';
            putchar('\n', current_color);
            break;
        } else if (key == '\b') {
            if (pos > 0) {
                pos--;
                putchar('\b', current_color);
            }
        } else if (key >= 32 && key <= 126) {  // Printable characters
            buffer[pos++] = key;
            putchar(key, current_color);
        }
    }
    buffer[pos] = '\0';
}

// Halt the system
void shutdown() {
    clear_screen();
    println("System halted.", RED_ON_BLACK);
    __asm__ __volatile__("cli");
    __asm__ __volatile__("hlt");
    while(1);
}

// Reboot the system
void reboot() {
    clear_screen();
    println("Rebooting...", YELLOW_ON_BLACK);
    
    // Small delay
    for(volatile int i = 0; i < 10000000; i++);
    
    // Triple fault to force reboot
    __asm__ __volatile__(
        "lidt (%0)"
        :
        : "r"(0)
    );
}

// Simple atoi implementation
int atoi(const char* str) {
    int result = 0;
    int sign = 1;
    int i = 0;
    
    // Handle negative numbers
    if (str[0] == '-') {
        sign = -1;
        i = 1;
    }
    
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }
    
    return sign * result;
}

// Simple itoa implementation
void itoa(int num, char* str) {
    int i = 0;
    int is_negative = 0;
    
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }
    
    // Convert to string (reversed)
    do {
        str[i++] = (num % 10) + '0';
        num /= 10;
    } while (num > 0);
    
    if (is_negative) {
        str[i++] = '-';
    }
    
    str[i] = '\0';
    
    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

// Command: help
void cmd_help() {
    println("Available commands:", CYAN_ON_BLACK);
    println("  help     - Show this help message", WHITE_ON_BLACK);
    println("  clear    - Clear the screen", WHITE_ON_BLACK);
    println("  echo     - Echo text back to screen", WHITE_ON_BLACK);
    println("  color    - Change text colors", WHITE_ON_BLACK);
    println("  calc     - Simple calculator", WHITE_ON_BLACK);
    println("  memory   - Show memory info", WHITE_ON_BLACK);
    println("  about    - About this OS", WHITE_ON_BLACK);
    println("  reboot   - Restart the system", WHITE_ON_BLACK);
    println("  shutdown - Halt the system", WHITE_ON_BLACK);
}

// Command: about
void cmd_about() {
    println("SimpleShell OS v1.0", YELLOW_ON_BLACK);
    println("A minimal operating system with command interpreter", WHITE_ON_BLACK);
    println("Features:", CYAN_ON_BLACK);
    println("  - Protected mode operation", WHITE_ON_BLACK);
    println("  - VGA text mode display", WHITE_ON_BLACK);
    println("  - Keyboard driver with modifiers", WHITE_ON_BLACK);
    println("  - Simple command shell", WHITE_ON_BLACK);
    println("Created as a learning project", GREEN_ON_BLACK);
}

// Command: memory
void cmd_memory() {
    println("Memory Information:", CYAN_ON_BLACK);
    println("  Kernel loaded at: 0x1000", WHITE_ON_BLACK);
    println("  Stack pointer at: 0x90000", WHITE_ON_BLACK);
    println("  Video memory at:  0xB8000", WHITE_ON_BLACK);
    println("  Available RAM: Not detected", WHITE_ON_BLACK);
}

// Command: calc
void cmd_calc(char* args) {
    // Simple parsing - expects "num1 op num2"
    int num1 = 0, num2 = 0, result = 0;
    char op = 0;
    char temp[20];
    int i = 0, j = 0;
    
    // Skip spaces
    while (args[i] == ' ') i++;
    
    // Get first number
    j = 0;
    while (args[i] && args[i] != ' ' && j < 19) {
        temp[j++] = args[i++];
    }
    temp[j] = '\0';
    
    if (j == 0) {
        println("Usage: calc <num1> <op> <num2>", RED_ON_BLACK);
        println("Example: calc 10 + 5", WHITE_ON_BLACK);
        return;
    }
    
    num1 = atoi(temp);
    
    // Skip spaces
    while (args[i] == ' ') i++;
    
    // Get operator
    if (args[i]) {
        op = args[i++];
    } else {
        println("Missing operator", RED_ON_BLACK);
        return;
    }
    
    // Skip spaces
    while (args[i] == ' ') i++;
    
    // Get second number
    j = 0;
    while (args[i] && args[i] != ' ' && j < 19) {
        temp[j++] = args[i++];
    }
