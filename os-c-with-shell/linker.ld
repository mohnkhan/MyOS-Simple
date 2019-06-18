/* ===========================================================================
 *  linker.ld  —  Link script for the C kernel (flat binary)
 *
 *  Emits a raw binary (not ELF) whose first byte is the kernel entry, placed
 *  at physical 0x1000 — the exact address the bootloader loads to and calls.
 *  Sections are laid out .text, .rodata, .data, .bss from 0x1000 upward.
 *  ENTRY(_start) names the entry symbol (defined in kernel_entry.asm).
 * ===========================================================================
 */
ENTRY(_start)
OUTPUT_FORMAT(binary)

SECTIONS
{
    . = 0x1000;
    
    .text : {
        *(.text)
    }
    
    .rodata : {
        *(.rodata)
    }
    
    .data : {
        *(.data)
    }
    
    .bss : {
        *(.bss)
    }
}
