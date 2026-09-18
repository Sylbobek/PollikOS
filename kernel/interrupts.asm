bits 32
section .text
extern interrupt_dispatch
global load_gdt
load_gdt:
    mov eax, [esp+4]
    lgdt [eax]
    jmp 0x08:.reload
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    ret

%macro ISR 1
global isr%1
isr%1:
%if %1 != 8 && %1 != 10 && %1 != 11 && %1 != 12 && %1 != 13 && %1 != 14 && %1 != 17 && %1 != 21 && %1 != 29 && %1 != 30
    push dword 0
%endif
    push dword %1
    jmp interrupt_common
%endmacro
%assign i 0
%rep 48
ISR i
%assign i i+1
%endrep
ISR 128
interrupt_common:
    push ds
    push es
    push fs
    push gs
    pushad
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld
    push esp
    call interrupt_dispatch
    mov esp, eax
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8
    iretd
section .rodata
global isr_table
isr_table:
%assign i 0
%rep 48
    dd isr%+i
%assign i i+1
%endrep
    dd isr128

; Same executable copied into private 64 KiB segments at process creation.
; Offset 0x1000 is private writable memory; no kernel addresses are exposed.
global user_program_start, user_program_end
user_program_start:
    inc dword [0x1000]
    mov ecx, 100000
.work:
    dec ecx
    jnz .work
    mov eax, 1                 ; SYS_REPORT(counter), returns fault-test flag
    mov ebx, [0x1000]
    int 0x80
    test eax, eax
    jnz .fault
    jmp user_program_start
.fault:
    cli                       ; deliberately forbidden at CPL3, must fault
    jmp .fault
user_program_end:

