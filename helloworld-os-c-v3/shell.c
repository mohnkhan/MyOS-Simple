// ============================================================================
//  shell.c  —  Interactive command shell (kernel entry: shell_main)
//
//  The kernel that is actually built in this stage. Drives the 80x25 VGA text
//  display, reads the keyboard by polling the 8042 controller (ports 0x60/0x64),
//  and runs a command interpreter with line editing, history, and Tab-comple-
//  tion. Built-in commands cover help/echo/clear, a fixed-point calculator
//  (calc), system info (memory/stats/about), the RTC clock (time/date/clock/
//  uptime), and the cooperative process model (ps/run/kill/suspend/resume) —
//  18 commands total. Low-level inb/outb are inline asm; all string handling is
//  hand-written (no libc).
// ============================================================================
#include "keyboard.h"
#include "process.h"
#include "rtc.h"

#define VIDEO_MEMORY 0xb8000
#define MAX_CMD_LEN 128
#define HISTORY_SIZE 10

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

// Command history structure
typedef struct {
    char commands[HISTORY_SIZE][MAX_CMD_LEN];
    int count;        // Total commands in history
    int current;      // Current position in history
} CommandHistory;

// Available commands for tab completion
const char* available_commands[] = {
    "help", "clear", "echo", "calc", "memory", 
    "stats", "history", "about", "shutdown",
    "ps", "run", "kill", "suspend", "resume",
    "time", "date", "clock", "uptime"
};
const int num_commands = 18;

// Global state
int cursor_x = 0;
int cursor_y = 0;
KeyboardState kbd_state = {0, 0, 0, 0};
CommandHistory history = {{""}, 0, 0};

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

int strlen(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

void strcpy(char* dest, const char* src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

// Simple atoi implementation
int atoi(const char* str) {
    int result = 0;
    int sign = 1;
    int i = 0;
    
    // Skip spaces
    while (str[i] == ' ') i++;
    
    // Handle negative numbers
    if (str[i] == '-') {
        sign = -1;
        i++;
    } else if (str[i] == '+') {
        i++;
    }
    
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }
    
    return sign * result;
}

// Parse floating-point number from string
// Returns the number multiplied by 1000 for 3 decimal places precision
int parse_float(const char* str, int* has_decimal) {
    int result = 0;
    int decimal_places = 0;
    int sign = 1;
    int i = 0;
    *has_decimal = 0;
    
    // Skip spaces
    while (str[i] == ' ') i++;
    
    // Handle negative numbers
    if (str[i] == '-') {
        sign = -1;
        i++;
    } else if (str[i] == '+') {
        i++;
    }
    
    // Parse integer part
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }
    
    // Check for decimal point
    if (str[i] == '.') {
        *has_decimal = 1;
        i++;
        
        // Parse decimal part (up to 3 decimal places)
        while (str[i] >= '0' && str[i] <= '9' && decimal_places < 3) {
            result = result * 10 + (str[i] - '0');
            decimal_places++;
            i++;
        }
        
        // Skip remaining decimal digits if any
        while (str[i] >= '0' && str[i] <= '9') {
            i++;
        }
    }
    
    // Multiply by 10 to fill remaining decimal places
    while (decimal_places < 3) {
        result = result * 10;
        decimal_places++;
    }
    
    return sign * result;
}

// Convert float (stored as int * 1000) to string
void float_to_str(int num, char* str, int force_decimal) {
    if (num == 0) {
        if (force_decimal) {
            str[0] = '0';
            str[1] = '.';
            str[2] = '0';
            str[3] = '\0';
        } else {
            str[0] = '0';
            str[1] = '\0';
        }
        return;
    }
    
    int is_negative = 0;
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }
    
    int integer_part = num / 1000;
    int decimal_part = num % 1000;
    
    // Convert integer part
    char temp[20];
    int i = 0;
    
    if (integer_part == 0) {
        temp[i++] = '0';
    } else {
        int ip = integer_part;
        while (ip > 0) {
            temp[i++] = (ip % 10) + '0';
            ip /= 10;
        }
        // Reverse integer part
        for (int j = 0; j < i / 2; j++) {
            char t = temp[j];
            temp[j] = temp[i - 1 - j];
            temp[i - 1 - j] = t;
        }
    }
    
    // Copy to output
    int out_idx = 0;
    if (is_negative) {
        str[out_idx++] = '-';
    }
    
    for (int j = 0; j < i; j++) {
        str[out_idx++] = temp[j];
    }
    
    // Add decimal part if needed
    if (decimal_part != 0 || force_decimal) {
        str[out_idx++] = '.';
        
        // Convert decimal part (already in correct scale)
        int d1 = decimal_part / 100;
        int d2 = (decimal_part / 10) % 10;
        int d3 = decimal_part % 10;
        
        // Remove trailing zeros
        if (d3 != 0) {
            str[out_idx++] = d1 + '0';
            str[out_idx++] = d2 + '0';
            str[out_idx++] = d3 + '0';
        } else if (d2 != 0) {
            str[out_idx++] = d1 + '0';
            str[out_idx++] = d2 + '0';
        } else if (d1 != 0 || force_decimal) {
            str[out_idx++] = d1 + '0';
        }
    }
    
    str[out_idx] = '\0';
}

// Simple itoa implementation
void itoa(int num, char* str) {
    int i = 0;
    int is_negative = 0;
    
    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
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
    if (scancode == SCANCODE_UP) return 0x11;      // Up arrow
    if (scancode == SCANCODE_DOWN) return 0x12;    // Down arrow
    if (scancode == SCANCODE_TAB) return '\t';     // Tab key
    if (scancode == SCANCODE_ENTER) return '\n';
    if (scancode == SCANCODE_BACKSPACE) return '\b';
    
    return ascii_char;
}

// Check if string starts with prefix
int starts_with(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str != *prefix) return 0;
        str++;
        prefix++;
    }
    return 1;
}

// Find command completion
const char* find_completion(const char* partial, int* match_count) {
    const char* match = 0;
    *match_count = 0;
    
    for (int i = 0; i < num_commands; i++) {
        if (starts_with(available_commands[i], partial)) {
            match = available_commands[i];
            (*match_count)++;
        }
    }
    
    return (*match_count == 1) ? match : 0;
}

// Show all matching commands
void show_completions(const char* partial) {
    int matches = 0;
    println("", WHITE_ON_BLACK);
    print("Possible commands: ", CYAN_ON_BLACK);
    
    for (int i = 0; i < num_commands; i++) {
        if (starts_with(available_commands[i], partial)) {
            if (matches > 0) print(", ", WHITE_ON_BLACK);
            print(available_commands[i], YELLOW_ON_BLACK);
            matches++;
        }
    }
    
    if (matches == 0) {
        print("none", RED_ON_BLACK);
    }
    println("", WHITE_ON_BLACK);
}

// Add command to history
void add_to_history(const char* cmd) {
    if (strlen(cmd) == 0) return;  // Don't add empty commands
    
    // Don't add duplicate of last command
    if (history.count > 0 && strcmp(history.commands[history.count - 1], cmd) == 0) {
        return;
    }
    
    // If history is full, shift everything up
    if (history.count >= HISTORY_SIZE) {
        for (int i = 0; i < HISTORY_SIZE - 1; i++) {
            strcpy(history.commands[i], history.commands[i + 1]);
        }
        strcpy(history.commands[HISTORY_SIZE - 1], cmd);
    } else {
        strcpy(history.commands[history.count], cmd);
        history.count++;
    }
    
    history.current = history.count;  // Reset to end of history
}

// Clear current line from cursor position
void clear_line_from_cursor(int start_x) {
    cursor_x = start_x;
    for (int i = start_x; i < 80; i++) {
        putchar_at(' ', i, cursor_y, WHITE_ON_BLACK);
    }
    cursor_x = start_x;
}

void get_line(char* buffer, int max_len) {
    int pos = 0;
    char key;
    int prompt_len = cursor_x;  // Save prompt position
    int history_pos = history.count;  // Start at end of history
    
    buffer[0] = '\0';
    
    while (pos < max_len - 1) {
        key = get_key();
        
        if (key == 0) continue;
        
        if (key == '\n') {
            buffer[pos] = '\0';
            putchar('\n', WHITE_ON_BLACK);
            // Add non-empty commands to history
            if (pos > 0) {
                add_to_history(buffer);
            }
            break;
        } else if (key == '\t') {  // Tab key - command completion
            buffer[pos] = '\0';
            
            // Only complete if buffer contains text and no spaces (command only)
            int has_space = 0;
            for (int i = 0; i < pos; i++) {
                if (buffer[i] == ' ') {
                    has_space = 1;
                    break;
                }
            }
            
            if (pos > 0 && !has_space) {
                int match_count = 0;
                const char* completion = find_completion(buffer, &match_count);
                
                if (completion) {
                    // Single match - complete the command
                    clear_line_from_cursor(prompt_len);
                    strcpy(buffer, completion);
                    pos = strlen(buffer);
                    print(buffer, WHITE_ON_BLACK);
                } else if (match_count > 1) {
                    // Multiple matches - show options
                    show_completions(buffer);
                    // Redraw prompt and current input
                    print("shell> ", GREEN_ON_BLACK);
                    print(buffer, WHITE_ON_BLACK);
                }
            }
        } else if (key == '\b') {
            if (pos > 0) {
                pos--;
                putchar('\b', WHITE_ON_BLACK);
                buffer[pos] = '\0';
            }
        } else if (key == 0x11) {  // Up arrow - previous command
            if (history_pos > 0) {
                history_pos--;
                // Clear current line
                clear_line_from_cursor(prompt_len);
                // Copy history command to buffer
                strcpy(buffer, history.commands[history_pos]);
                pos = strlen(buffer);
                // Display the command
                print(buffer, WHITE_ON_BLACK);
            }
        } else if (key == 0x12) {  // Down arrow - next command
            if (history_pos < history.count - 1) {
                history_pos++;
                // Clear current line
                clear_line_from_cursor(prompt_len);
                // Copy history command to buffer
                strcpy(buffer, history.commands[history_pos]);
                pos = strlen(buffer);
                // Display the command
                print(buffer, WHITE_ON_BLACK);
            } else if (history_pos == history.count - 1) {
                // At end of history, clear the line
                history_pos = history.count;
                clear_line_from_cursor(prompt_len);
                buffer[0] = '\0';
                pos = 0;
            }
        } else if (key >= 32 && key <= 126) {
            buffer[pos++] = key;
            buffer[pos] = '\0';
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
    println("  calc     - Advanced calculator with floating-point", WHITE_ON_BLACK);
    println("           Examples: calc 3.14 * 2, calc 10.5 / 2.5", CYAN_ON_BLACK);
    println("  memory   - Display memory information", WHITE_ON_BLACK);
    println("  stats    - Show system statistics", WHITE_ON_BLACK);
    println("  history  - Show command history", WHITE_ON_BLACK);
    println("  about    - About this OS", WHITE_ON_BLACK);
    println("  shutdown - Halt the system", WHITE_ON_BLACK);
    println("", WHITE_ON_BLACK);
    println("Date & Time:", CYAN_ON_BLACK);
    println("  time     - Display current time", WHITE_ON_BLACK);
    println("  date     - Display current date", WHITE_ON_BLACK);
    println("  clock    - Display full date and time", WHITE_ON_BLACK);
    println("  uptime   - Display system uptime since boot", WHITE_ON_BLACK);
    println("", WHITE_ON_BLACK);
    println("Process Management:", CYAN_ON_BLACK);
    println("  ps       - List all processes", WHITE_ON_BLACK);
    println("  run      - Run a sample process (counter/fibonacci/prime)", WHITE_ON_BLACK);
    println("  kill     - Kill a process by PID", WHITE_ON_BLACK);
    println("  suspend  - Suspend a process by PID", WHITE_ON_BLACK);
    println("  resume   - Resume a suspended process", WHITE_ON_BLACK);
    println("", WHITE_ON_BLACK);
    println("Navigation:", CYAN_ON_BLACK);
    println("  UP/DOWN arrows - Navigate command history", WHITE_ON_BLACK);
    println("  TAB            - Auto-complete command names", WHITE_ON_BLACK);
    println("  BACKSPACE      - Delete character", WHITE_ON_BLACK);
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

// Advanced calculator with floating-point support
void cmd_calc(char* args) {
    char num1_str[30];
    char num2_str[30];
    char op = 0;
    int i = 0, j = 0;
    
    // Skip spaces
    while (args[i] == ' ') i++;
    
    // Get first number (can be decimal)
    j = 0;
    int is_negative = 0;
    if (args[i] == '-' && args[i+1] != ' ') {
        is_negative = 1;
        i++;
    }
    
    while (args[i] && args[i] != ' ' && args[i] != '+' && args[i] != '-' && 
           args[i] != '*' && args[i] != '/' && j < 28) {
        num1_str[j++] = args[i++];
    }
    num1_str[j] = '\0';
    
    if (j == 0) {
        println("Advanced Calculator Usage:", CYAN_ON_BLACK);
        println("  calc <num1> <op> <num2>", WHITE_ON_BLACK);
        println("", WHITE_ON_BLACK);
        println("Examples:", CYAN_ON_BLACK);
        println("  calc 10 + 5       (integers)", WHITE_ON_BLACK);
        println("  calc 3.14 * 2     (decimals)", WHITE_ON_BLACK);
        println("  calc 10.5 / 2.5   (floating-point)", WHITE_ON_BLACK);
        println("  calc -5.2 + 3.7   (negative numbers)", WHITE_ON_BLACK);
        println("", WHITE_ON_BLACK);
        println("Operators: + - * /", WHITE_ON_BLACK);
        println("Precision: Up to 3 decimal places", YELLOW_ON_BLACK);
        return;
    }
    
    // Prepend negative sign if needed
    if (is_negative) {
        for (int k = j; k >= 0; k--) {
            num1_str[k+1] = num1_str[k];
        }
        num1_str[0] = '-';
    }
    
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
    
    // Get second number (can be decimal)
    j = 0;
    while (args[i] && args[i] != ' ' && j < 29) {
        num2_str[j++] = args[i++];
    }
    num2_str[j] = '\0';
    
    if (j == 0) {
        println("Missing second number", RED_ON_BLACK);
        return;
    }
    
    // Parse floating-point numbers (stored as fixed-point * 1000)
    int has_decimal1 = 0, has_decimal2 = 0;
    int num1 = parse_float(num1_str, &has_decimal1);
    int num2 = parse_float(num2_str, &has_decimal2);
    int result = 0;
    int has_decimal_result = has_decimal1 || has_decimal2;
    
    // Perform calculation
    char result_str[40];
    char full_result[100];
    
    switch(op) {
        case '+':
            result = num1 + num2;
            break;
            
        case '-':
            result = num1 - num2;
            break;
            
        case '*': {
            // Multiply with fixed-point adjustment
            // For simplicity and to avoid overflow, we'll use a simpler approach
            // that works for reasonable numbers
            if (num1 < 0) num1 = -num1;
            if (num2 < 0) num2 = -num2;
            
            // Perform multiplication in parts to avoid overflow
            int temp_high = (num1 / 1000) * num2;  // Integer part * num2
            int temp_low = (num1 % 1000) * (num2 / 1000);  // Fraction * integer part of num2
            int temp_frac = ((num1 % 1000) * (num2 % 1000)) / 1000;  // Fraction * fraction
            
            result = temp_high + temp_low + temp_frac;
            
            // Handle sign
            char num1_str_temp[30];
            strcpy(num1_str_temp, num1_str);
            char num2_str_temp[30];
            strcpy(num2_str_temp, num2_str);
            int sign1 = (num1_str_temp[0] == '-') ? -1 : 1;
            int sign2 = (num2_str_temp[0] == '-') ? -1 : 1;
            if (sign1 * sign2 < 0) result = -result;
            break;
        }
            
        case '/': {
            if (num2 == 0) {
                println("Error: Division by zero!", RED_ON_BLACK);
                return;
            }
            
            // Handle signs
            int sign = 1;
            if (num1 < 0) {
                num1 = -num1;
                sign = -sign;
            }
            if (num2 < 0) {
                num2 = -num2;
                sign = -sign;
            }
            
            // Division with fixed-point
            // We want (num1/num2) * 1000
            // First scale up num1 by additional 1000 to maintain precision
            // But we need to avoid overflow, so we'll do it in steps
            
            // Simple approach for division:
            // result = (num1 * 1000) / num2
            // But this can overflow, so we do it carefully
            
            // Integer division first
            int quotient = num1 / num2;  // This gives us the quotient in fixed-point
            int remainder = num1 % num2;
            
            // Now scale the remainder and divide
            int fractional = (remainder * 1000) / num2;
            
            result = quotient * 1000 + fractional;
            result = result * sign;
            
            has_decimal_result = 1;  // Division always produces decimal result
            break;
        }
            
        default:
            print("Unknown operator: ", RED_ON_BLACK);
            char op_str[2] = {op, '\0'};
            println(op_str, RED_ON_BLACK);
            return;
    }
    
    // Format the output
    float_to_str(num1, num1_str, has_decimal1);
    float_to_str(num2, num2_str, has_decimal2);
    float_to_str(result, result_str, has_decimal_result);
    
    // Build full result string
    int len = 0;
    for (int k = 0; num1_str[k]; k++) {
        full_result[len++] = num1_str[k];
    }
    full_result[len++] = ' ';
    full_result[len++] = op;
    full_result[len++] = ' ';
    for (int k = 0; num2_str[k]; k++) {
        full_result[len++] = num2_str[k];
    }
    full_result[len++] = ' ';
    full_result[len++] = '=';
    full_result[len++] = ' ';
    for (int k = 0; result_str[k]; k++) {
        full_result[len++] = result_str[k];
    }
    full_result[len] = '\0';
    
    println(full_result, YELLOW_ON_BLACK);
}

// Memory info command
void cmd_memory() {
    println("Memory Information:", CYAN_ON_BLACK);
    println("  Kernel loaded at: 0x1000", WHITE_ON_BLACK);
    println("  Stack pointer at: 0x90000", WHITE_ON_BLACK);
    println("  Video memory at:  0xB8000", WHITE_ON_BLACK);
    println("  Boot sector at:   0x7C00", WHITE_ON_BLACK);
    println("  GDT location:     Protected mode enabled", WHITE_ON_BLACK);
    println("  Kernel size:      ~10KB", WHITE_ON_BLACK);
    println("  Available RAM:    640KB (conventional memory)", WHITE_ON_BLACK);
    println("  Video RAM:        4KB (text mode buffer)", WHITE_ON_BLACK);
}

// System statistics command
void cmd_stats() {
    println("System Statistics:", CYAN_ON_BLACK);
    println("  OS Version:       SimpleShell OS v1.0", WHITE_ON_BLACK);
    println("  Architecture:     x86 (32-bit protected mode)", WHITE_ON_BLACK);
    println("  Display:          VGA Text Mode (80x25)", WHITE_ON_BLACK);
    println("  Colors:           16 colors available", WHITE_ON_BLACK);
    println("  Keyboard:         PS/2 compatible", WHITE_ON_BLACK);
    println("  Boot Device:      Hard Disk (0x80)", WHITE_ON_BLACK);
    println("  CPU Mode:         Protected Mode", WHITE_ON_BLACK);
    println("  Interrupts:       Disabled (CLI)", WHITE_ON_BLACK);
    
    // Show current cursor position
    print("  Cursor Position:  Row ", WHITE_ON_BLACK);
    char pos_str[10];
    itoa(cursor_y, pos_str);
    print(pos_str, YELLOW_ON_BLACK);
    print(", Col ", WHITE_ON_BLACK);
    itoa(cursor_x, pos_str);
    println(pos_str, YELLOW_ON_BLACK);
    
    // Show command history stats
    print("  History Count:    ", WHITE_ON_BLACK);
    itoa(history.count, pos_str);
    print(pos_str, YELLOW_ON_BLACK);
    print(" / ", WHITE_ON_BLACK);
    itoa(HISTORY_SIZE, pos_str);
    println(pos_str, YELLOW_ON_BLACK);
    
    println("  Shell Status:     Active", GREEN_ON_BLACK);
}

// Command history display
void cmd_history() {
    if (history.count == 0) {
        println("No commands in history", YELLOW_ON_BLACK);
        return;
    }
    
    println("Command History:", CYAN_ON_BLACK);
    char num_str[10];
    for (int i = 0; i < history.count; i++) {
        print("  ", WHITE_ON_BLACK);
        itoa(i + 1, num_str);
        print(num_str, YELLOW_ON_BLACK);
        print(". ", WHITE_ON_BLACK);
        println(history.commands[i], WHITE_ON_BLACK);
    }
    
    print("Total: ", CYAN_ON_BLACK);
    itoa(history.count, num_str);
    print(num_str, YELLOW_ON_BLACK);
    print(" commands (max ", WHITE_ON_BLACK);
    itoa(HISTORY_SIZE, num_str);
    print(num_str, YELLOW_ON_BLACK);
    println(")", WHITE_ON_BLACK);
}

void shutdown() {
    clear_screen();
    println("System halted.", RED_ON_BLACK);
    __asm__ __volatile__("cli");
    __asm__ __volatile__("hlt");
    while(1);
}

// Command: time - Display current time
void cmd_time() {
    char time_str[20];
    rtc_get_time_string(time_str);
    print("Current time: ", CYAN_ON_BLACK);
    println(time_str, YELLOW_ON_BLACK);
}

// Command: date - Display current date
void cmd_date() {
    char date_str[20];
    rtc_time_t time;
    
    rtc_get_date_string(date_str);
    rtc_read_time(&time);
    
    print("Current date: ", CYAN_ON_BLACK);
    print(date_str, YELLOW_ON_BLACK);
    
    // Add weekday
    print(" (", WHITE_ON_BLACK);
    extern const char* get_weekday_name(uint8_t weekday);
    print(get_weekday_name(time.weekday), GREEN_ON_BLACK);
    println(")", WHITE_ON_BLACK);
}

// Command: clock - Display full date and time
void cmd_clock() {
    char time_str[20];
    char date_str[20];
    rtc_time_t time;
    
    rtc_get_time_string(time_str);
    rtc_get_date_string(date_str);
    rtc_read_time(&time);
    
    println("=== System Clock ===", CYAN_ON_BLACK);
    
    // Display date with weekday and month name
    extern const char* get_weekday_name(uint8_t weekday);
    extern const char* get_month_name(uint8_t month);
    
    print("  Date: ", WHITE_ON_BLACK);
    print(get_weekday_name(time.weekday), GREEN_ON_BLACK);
    print(", ", WHITE_ON_BLACK);
    
    char day_str[3];
    itoa(time.day, day_str);
    print(day_str, YELLOW_ON_BLACK);
    print(" ", WHITE_ON_BLACK);
    print(get_month_name(time.month), YELLOW_ON_BLACK);
    print(" ", WHITE_ON_BLACK);
    char year_str[5];
    itoa(time.year, year_str);
    println(year_str, YELLOW_ON_BLACK);
    
    print("  Time: ", WHITE_ON_BLACK);
    println(time_str, YELLOW_ON_BLACK);
    
    // Display in standard format as well
    print("  Format: ", WHITE_ON_BLACK);
    print(date_str, CYAN_ON_BLACK);
    print(" ", WHITE_ON_BLACK);
    println(time_str, CYAN_ON_BLACK);
}

// Command: uptime - Display system uptime
void cmd_uptime() {
    char uptime_str[50];
    uint32_t uptime_secs = rtc_get_uptime_seconds();
    
    rtc_get_uptime_string(uptime_str);
    
    print("System uptime: ", CYAN_ON_BLACK);
    println(uptime_str, YELLOW_ON_BLACK);
    
    // Also show total seconds
    print("Total seconds: ", WHITE_ON_BLACK);
    char secs_str[12];
    itoa(uptime_secs, secs_str);
    println(secs_str, GREEN_ON_BLACK);
}

// Main shell loop
void shell_main() {
    char input[MAX_CMD_LEN];
    char cmd[64];
    char args[MAX_CMD_LEN];
    
    // Initialize RTC and record boot time
    rtc_init();
    rtc_record_boot_time();
    
    clear_screen();
    println("SimpleShell OS v1.0", YELLOW_ON_BLACK);
    println("Type 'help' for available commands", CYAN_ON_BLACK);
    
    // Display current time at startup
    char time_str[20];
    char date_str[20];
    rtc_get_time_string(time_str);
    rtc_get_date_string(date_str);
    print("System Time: ", WHITE_ON_BLACK);
    print(date_str, GREEN_ON_BLACK);
    print(" ", WHITE_ON_BLACK);
    println(time_str, GREEN_ON_BLACK);
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
        } else if (strcmp(cmd, "calc") == 0) {
            cmd_calc(args);
        } else if (strcmp(cmd, "memory") == 0) {
            cmd_memory();
        } else if (strcmp(cmd, "stats") == 0) {
            cmd_stats();
        } else if (strcmp(cmd, "history") == 0) {
            cmd_history();
        } else if (strcmp(cmd, "about") == 0) {
            cmd_about();
        } else if (strcmp(cmd, "shutdown") == 0) {
            shutdown();
        } else if (strcmp(cmd, "ps") == 0) {
            process_list();
        } else if (strcmp(cmd, "run") == 0) {
            // Run sample processes
            if (strcmp(args, "counter") == 0) {
                process_create("counter", process_counter, 1);
            } else if (strcmp(args, "fibonacci") == 0) {
                process_create("fibonacci", process_fibonacci, 2);
            } else if (strcmp(args, "prime") == 0) {
                process_create("prime", process_prime, 3);
            } else if (args[0] == '\0') {
                println("Usage: run <process>", CYAN_ON_BLACK);
                println("Available processes:", WHITE_ON_BLACK);
                println("  counter    - Counting process", WHITE_ON_BLACK);
                println("  fibonacci  - Fibonacci sequence generator", WHITE_ON_BLACK);
                println("  prime      - Prime number finder", WHITE_ON_BLACK);
            } else {
                print("Unknown process: ", RED_ON_BLACK);
                println(args, RED_ON_BLACK);
            }
        } else if (strcmp(cmd, "kill") == 0) {
            if (args[0] == '\0') {
                println("Usage: kill <pid>", CYAN_ON_BLACK);
            } else {
                int pid = atoi(args);
                process_kill(pid);
            }
        } else if (strcmp(cmd, "suspend") == 0) {
            if (args[0] == '\0') {
                println("Usage: suspend <pid>", CYAN_ON_BLACK);
            } else {
                int pid = atoi(args);
                process_suspend(pid);
            }
        } else if (strcmp(cmd, "resume") == 0) {
            if (args[0] == '\0') {
                println("Usage: resume <pid>", CYAN_ON_BLACK);
            } else {
                int pid = atoi(args);
                process_resume(pid);
            }
        } else if (strcmp(cmd, "time") == 0) {
            cmd_time();
        } else if (strcmp(cmd, "date") == 0) {
            cmd_date();
        } else if (strcmp(cmd, "clock") == 0) {
            cmd_clock();
        } else if (strcmp(cmd, "uptime") == 0) {
            cmd_uptime();
        } else {
            print("Unknown command: ", RED_ON_BLACK);
            println(cmd, RED_ON_BLACK);
            println("Type 'help' for available commands", CYAN_ON_BLACK);
        }
    }
}

// Entry point for the kernel - called from kernel_entry.asm
void kernel_main() {
    // Initialize process management
    process_init();
    
    // Start the shell
    shell_main();
}
