ENTRY(_start)
SECTIONS {
    . = 0x100000;
    __kernel_start = .;
    __text_start = .;
    .text : { KEEP(*(.entry)) *(.text*) }
    . = ALIGN(4096);
    __text_end = .;
    __rodata_start = .;
    .rodata : { *(.rodata*) }
    . = ALIGN(4096);
    __rodata_end = .;
    .data : { *(.data*) }
    . = ALIGN(4096);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    . = ALIGN(4096);
    __bss_end = .;
    ASSERT(__bss_end <= 0x1fe000, "Bootstrap overlaps physical access aperture")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
