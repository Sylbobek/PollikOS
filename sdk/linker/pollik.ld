ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x8000000000;
    .text : { *(.text*) *(.ltext*) *(.rodata*) *(.lrodata*) } :code
    . = 0x8000200000;
    .data : { *(.data*) *(.ldata*) } :data
    .bss (NOLOAD) : { *(.bss*) *(.lbss*) } :data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
