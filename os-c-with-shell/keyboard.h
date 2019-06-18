// ============================================================================
//  keyboard.h  —  PS/2 keyboard scancodes and translation tables
//
//  Definitions for reading the 8042 keyboard controller in protected mode,
//  where BIOS input services are unavailable. Provides the data/status port
//  numbers (0x60 / 0x64), status-bit flags, a KeyboardState struct for modifier
//  tracking, the Set-1 scancode-to-ASCII tables (normal and shifted), and named
//  constants for the special scancodes. The high bit (0x80) in a scancode marks
//  a key-release event.
// ============================================================================
#ifndef KEYBOARD_H
#define KEYBOARD_H

// Keyboard ports
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// Keyboard status flags
#define KEYBOARD_OUTPUT_FULL 0x01
#define KEYBOARD_INPUT_FULL 0x02

// Special key states
typedef struct {
    unsigned char shift_pressed;
    unsigned char ctrl_pressed;
    unsigned char alt_pressed;
    unsigned char caps_lock;
} KeyboardState;

// Scancode to ASCII mappings
// Normal keys (without shift)
static const char scancode_to_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',  // 0-9
    '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r',  // 10-19
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,  // 20-29
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',  // 30-39
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n',  // 40-49
    'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0,  // 50-59
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 60-69
    0, 0, 0, 0, '-', 0, 0, 0, '+', 0,  // 70-79
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 80-89
};

// Shifted keys
static const char scancode_to_ascii_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*',  // 0-9
    '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R',  // 10-19
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,  // 20-29
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',  // 30-39
    '"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N',  // 40-49
    'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0,  // 50-59
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 60-69
    0, 0, 0, 0, '-', 0, 0, 0, '+', 0,  // 70-79
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 80-89
};

// Special scancodes
#define SCANCODE_LEFT_SHIFT 0x2A
#define SCANCODE_RIGHT_SHIFT 0x36
#define SCANCODE_LEFT_CTRL 0x1D
#define SCANCODE_LEFT_ALT 0x38
#define SCANCODE_CAPS_LOCK 0x3A
#define SCANCODE_F1 0x3B
#define SCANCODE_F2 0x3C
#define SCANCODE_F3 0x3D
#define SCANCODE_F4 0x3E
#define SCANCODE_F5 0x3F
#define SCANCODE_F6 0x40
#define SCANCODE_F7 0x41
#define SCANCODE_F8 0x42
#define SCANCODE_F9 0x43
#define SCANCODE_F10 0x44
#define SCANCODE_UP 0x48
#define SCANCODE_LEFT 0x4B
#define SCANCODE_RIGHT 0x4D
#define SCANCODE_DOWN 0x50
#define SCANCODE_HOME 0x47
#define SCANCODE_END 0x4F
#define SCANCODE_PAGE_UP 0x49
#define SCANCODE_PAGE_DOWN 0x51
#define SCANCODE_INSERT 0x52
#define SCANCODE_DELETE 0x53
#define SCANCODE_ESC 0x01
#define SCANCODE_ENTER 0x1C
#define SCANCODE_BACKSPACE 0x0E
#define SCANCODE_TAB 0x0F
#define SCANCODE_SPACE 0x39

// Key release bit
#define KEY_RELEASE_BIT 0x80

#endif // KEYBOARD_H
