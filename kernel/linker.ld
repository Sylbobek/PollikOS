ENTRY(_start)
SECTIONS {
    /* Stage 2 copies the flat image to 1 MiB; BSS follows the image and is
     * zeroed by entry.asm. Everything must stay below the kernel heap limit
     * (mem.c HEAP_MAX) and the legacy worker area at 0x800000. */
    . = 0x100000;
    __kernel_start = .;
    .text : { *(.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(4096);
    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(4096);
    __bss_end = .;
    ASSERT(__bss_end < 0x600000, "Kernel image + BSS leaves less than 2 MiB of kernel heap below 0x800000")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
