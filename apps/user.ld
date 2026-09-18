ENTRY(_start)
SECTIONS
{
    /* Must lie inside [USER_SPACE_START, USER_SPACE_END) from kernel/vmm.h:
     * user space starts at 1 GiB, above the kernel's direct map. */
    . = 0x40001000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    . = ALIGN(4096);
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(4096);
}
