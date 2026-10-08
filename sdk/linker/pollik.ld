ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x8000000000;
    .text : { *(.text*) *(.ltext*) *(.rodata*) *(.lrodata*) } :code
    /* Keep the historical data address for small programs. Larger native
     * engines get a page-separated RW/NX segment after their RX image. */
    . = MAX(0x8000200000, ALIGN(4096));
    .data : { *(.data*) *(.ldata*) } :data
    .bss (NOLOAD) : { *(.bss*) *(.lbss*) } :data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
