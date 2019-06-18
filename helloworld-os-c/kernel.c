// ============================================================================
//  kernel.c  —  Protected-mode C kernel: VGA color demo + keyboard
//
//  The C entry point (kernel_main) for this stage, compiled freestanding and
//  linked at 0x1000. Defines the 16 VGA colors and a make_color(bg,fg) helper,
//  writes characters as (char, attribute) pairs directly to video memory at
//  0xB8000, renders a color showcase, and reads the keyboard via scancodes.
//  No libc: no printf, no malloc, no startup code.
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
#define YELLOW_ON_BLUE  0x1E
#define GREEN_ON_BLACK  0x02
#define CYAN_ON_BLACK   0x03
#define RED_ON_BLACK    0x04
#define WHITE_ON_BLUE   0x1F
#define WHITE_ON_RED    0x4F
#define MAGENTA_ON_BLACK 0x05
#define LIGHT_GREEN_ON_BLACK 0x0A
#define YELLOW_ON_BLACK 0x0E

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

// Function to print a string at specific position with color
void print_at_color(const char* str, int x, int y, char color) {
    int i = 0;
    while (str[i] != '\0') {
        putchar_at(str[i], x + i, y, color);
        i++;
    }
}

// Function to print a string at specific position (default white on black)
void print_at(const char* str, int x, int y) {
    print_at_color(str, x, y, WHITE_ON_BLACK);
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

// Clear the screen (default blue background)
void clear_screen() {
    clear_screen_color(BLUE);
}

// Draw a colored box
void draw_box(int x, int y, int width, int height, char color) {
    // Top and bottom borders
    for (int i = 0; i < width; i++) {
        putchar_at('-', x + i, y, color);
        putchar_at('-', x + i, y + height - 1, color);
    }
    // Left and right borders
    for (int i = 0; i < height; i++) {
        putchar_at('|', x, y + i, color);
        putchar_at('|', x + width - 1, y + i, color);
    }
    // Corners
    putchar_at('+', x, y, color);
    putchar_at('+', x + width - 1, y, color);
    putchar_at('+', x, y + height - 1, color);
    putchar_at('+', x + width - 1, y + height - 1, color);
}

// Global keyboard state
KeyboardState kbd_state = {0, 0, 0, 0};
int cursor_x = 40;
int cursor_y = 16;

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
    if (scancode == SCANCODE_UP) return 'U';
    if (scancode == SCANCODE_DOWN) return 'D';
    if (scancode == SCANCODE_LEFT) return 'L';
    if (scancode == SCANCODE_RIGHT) return 'R';
    if (scancode == SCANCODE_F1) return '1';
    if (scancode == SCANCODE_F2) return '2';
    if (scancode == SCANCODE_ESC) return 27;
    if (scancode == SCANCODE_ENTER) return '\n';
    if (scancode == SCANCODE_BACKSPACE) return '\b';
    if (scancode == SCANCODE_TAB) return '\t';
    
    return ascii_char;
}

// Simple keyboard function for compatibility
char get_key() {
    char key;
    do {
        key = get_key_advanced();
    } while (key == 0);
    return key;
}

// Display current keyboard state
void show_keyboard_status(int x, int y) {
    char status[50];
    int i = 0;
    
    // Clear status line
    for (i = 0; i < 50; i++) {
        putchar_at(' ', x + i, y, WHITE_ON_BLACK);
    }
    
    // Build status string
    i = 0;
    status[i++] = '[';
    if (kbd_state.shift_pressed) {
        status[i++] = 'S';
        status[i++] = 'H';
        status[i++] = 'I';
        status[i++] = 'F';
        status[i++] = 'T';
        status[i++] = ' ';
    }
    if (kbd_state.ctrl_pressed) {
        status[i++] = 'C';
        status[i++] = 'T';
        status[i++] = 'R';
        status[i++] = 'L';
        status[i++] = ' ';
    }
    if (kbd_state.alt_pressed) {
        status[i++] = 'A';
        status[i++] = 'L';
        status[i++] = 'T';
        status[i++] = ' ';
    }
    if (kbd_state.caps_lock) {
        status[i++] = 'C';
        status[i++] = 'A';
        status[i++] = 'P';
        status[i++] = 'S';
        status[i++] = ' ';
    }
    status[i++] = ']';
    status[i] = '\0';
    
    // Display status
    print_at_color(status, x, y, YELLOW_ON_BLUE);
}

// Halt the system
void shutdown() {
    // Disable interrupts and halt
    __asm__ __volatile__("cli");
    __asm__ __volatile__("hlt");
    // Infinite loop as backup
    while(1);
}

// Entry point for the kernel - called from kernel_entry.asm
void kernel_main() {
    char input_buffer[60];
    int buffer_pos = 0;
    
    // Clear the screen with blue background
    clear_screen_color(BLUE);
    
    // Draw decorative box around the main content
    draw_box(10, 6, 60, 14, YELLOW_ON_BLUE);
    
    // Display colorful title
    print_at_color("== Advanced Keyboard Demo OS ==", 24, 3, YELLOW_ON_BLUE);
    
    // Display "Hello, World!" in white on black (high contrast)
    print_at_color("Hello, World!", 34, 8, BLACK_ON_WHITE);
    
    // Display instructions
    print_at_color("Type to test keyboard input:", 26, 11, LIGHT_GREEN_ON_BLACK);
    print_at_color("ESC = Clear | Q = Quit | F1 = Help", 22, 13, CYAN_ON_BLACK);
    print_at_color("Use SHIFT, CTRL, ALT, CAPS LOCK", 24, 14, CYAN_ON_BLACK);
    
    // Input area
    draw_box(15, 15, 50, 3, WHITE_ON_BLUE);
    cursor_x = 16;
    cursor_y = 16;
    
    // Display keyboard status
    print_at_color("Modifiers:", 15, 19, WHITE_ON_BLACK);
    
    // Display color palette at bottom
    print_at_color("Colors:", 5, 23, WHITE_ON_BLACK);
    for (int i = 0; i < 16; i++) {
        putchar_at(' ', 13 + i*2, 23, make_color(i, i));
        putchar_at(' ', 13 + i*2 + 1, 23, make_color(i, i));
    }
    
    // Main input loop
    while(1) {
        // Show keyboard status
        show_keyboard_status(26, 19);
        
        // Show cursor
        putchar_at('_', cursor_x, cursor_y, WHITE_ON_BLACK);
        
        // Get key
        char key = get_key_advanced();
        
        if (key != 0) {
            // Clear cursor
            putchar_at(' ', cursor_x, cursor_y, WHITE_ON_BLACK);
            
            // Handle special keys
            if (key == 'Q' || key == 'q') {
                if (kbd_state.ctrl_pressed) {
                    // Ctrl+Q to quit
                    clear_screen_color(RED);
                    print_at_color("Shutting down...", 32, 12, WHITE_ON_RED);
                    for(volatile int i = 0; i < 10000000; i++);
                    shutdown();
                } else {
                    // Normal Q - just display it
                    if (cursor_x < 64 && buffer_pos < 59) {
                        putchar_at(key, cursor_x++, cursor_y, WHITE_ON_BLACK);
                        input_buffer[buffer_pos++] = key;
                    }
                }
            } else if (key == 27) { // ESC - clear input
                for (int i = 16; i < 64; i++) {
                    putchar_at(' ', i, cursor_y, WHITE_ON_BLACK);
                }
                cursor_x = 16;
                buffer_pos = 0;
            } else if (key == '\b') { // Backspace
                if (cursor_x > 16 && buffer_pos > 0) {
                    cursor_x--;
                    buffer_pos--;
                    putchar_at(' ', cursor_x, cursor_y, WHITE_ON_BLACK);
                }
            } else if (key == '\n') { // Enter - new line effect
                cursor_x = 16;
                buffer_pos = 0;
                for (int i = 16; i < 64; i++) {
                    putchar_at(' ', i, cursor_y, WHITE_ON_BLACK);
                }
            } else if (key == '1' && kbd_state.alt_pressed) { // F1 - Help
                print_at_color("Help: Type text, use modifiers!", 20, 21, YELLOW_ON_BLACK);
            } else if (key == 'U') { // Arrow up
                print_at_color("UP   ", 70, 19, GREEN_ON_BLACK);
            } else if (key == 'D') { // Arrow down
                print_at_color("DOWN ", 70, 19, GREEN_ON_BLACK);
            } else if (key == 'L') { // Arrow left
                if (cursor_x > 16) cursor_x--;
            } else if (key == 'R') { // Arrow right
                if (cursor_x < 64) cursor_x++;
            } else if (key >= 32 && key <= 126) { // Printable characters
                if (cursor_x < 64 && buffer_pos < 59) {
                    // Display with color based on modifiers
                    char color = WHITE_ON_BLACK;
                    if (kbd_state.ctrl_pressed) color = RED_ON_BLACK;
                    else if (kbd_state.alt_pressed) color = GREEN_ON_BLACK;
                    else if (kbd_state.shift_pressed) color = YELLOW_ON_BLACK;
                    
                    putchar_at(key, cursor_x++, cursor_y, color);
                    input_buffer[buffer_pos++] = key;
                }
            }
        }
    }
}
