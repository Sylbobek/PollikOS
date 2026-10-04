bits 64
%include "user_abi.inc"
section .text
extern exception64
global syscall64_entry
syscall64_entry:
    swapgs
    lfence
    mov [gs:CPU_ENTRY_USER_RSP], rsp
    mov rsp, [gs:CPU_ENTRY_KERNEL_RSP]
    push qword USER_SS
    push qword [gs:CPU_ENTRY_USER_RSP]
    push r11
    push qword USER_CS
    push rcx
    push qword 0
    push qword USER_SYSCALL_VECTOR
    swapgs                       ; C, IRQ and NMI code never depend on GS
    jmp exception_common

global enter_dynamic_stack
enter_dynamic_stack:
    mov rsp, rdi
    and rsp, -16
    xor ebp, ebp
    call rsi
    ud2
global process64_enter, process64_leave
process64_enter:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rsi], rsp
    mov rsp, rdi             ; validated UserFrame in supervisor control page
    jmp restore_frame
process64_leave:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, rdi             ; saved kernel continuation, never user supplied
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
%assign i 0
%rep 256
isr%+i:
%if i != 8 && i != 10 && i != 11 && i != 12 && i != 13 && i != 14 && i != 17 && i != 21 && i != 29 && i != 30
    push qword 0
%endif
    push qword i
    jmp exception_common
%assign i i+1
%endrep
exception_common:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld
    mov rdi, rsp
    mov rbx, rsp
    and rsp, -16
    call exception64
    mov rsp, rbx
restore_frame:
    cmp qword [rsp+USER_FRAME_VECTOR], USER_SYSCALL_VECTOR
    je syscall64_return
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    add rsp, 16
    iretq

; Reached only after process64_trap validates executable user RIP, writable
; user RSP and selectors, and sanitizes RFLAGS. IF stays clear until SYSRETQ.
syscall64_return:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    mov rcx, [rsp+16]             ; validated RIP
    mov r11, [rsp+32]             ; sanitized flags
    mov rsp, [rsp+40]             ; validated user RSP; no more stack access
    o64 sysret

%ifdef SELFTEST
global probe_registers, probe_read, probe_write, probe_execute, probe_return
global probe_read_instruction, probe_write_instruction, probe_double_fault
probe_registers:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov rax, 0x1234567800000001
    mov rcx, 0x1234567800000002
    mov rdx, 0x1234567800000003
    mov rbx, 0x1234567800000004
    mov rbp, 0x1234567800000005
    mov rsi, 0x1234567800000006
    mov rdi, 0x1234567800000007
    mov r8,  0x1234567800000008
    mov r9,  0x1234567800000009
    mov r10, 0x123456780000000a
    mov r11, 0x123456780000000b
    mov r12, 0x123456780000000c
    mov r13, 0x123456780000000d
    mov r14, 0x123456780000000e
    mov r15, 0x123456780000000f
    std ; entry must clear DF for C and IRET must restore it
    int3
    ; A second trap validates register restoration by the first IRET.
    int3
    cld
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
probe_read:
probe_read_instruction:
    mov al, [rdi]
    ret
probe_write:
probe_write_instruction:
    mov byte [rdi], 0
    ret
probe_execute:
    jmp rdi
probe_return:
    ret
probe_double_fault:
    mov rsp, rdi
    xor eax, eax
    mov byte [rax], 0 ; #PF delivery on an unmapped stack escalates to #DF
    ud2
%endif
section .rodata
global isr64_table
isr64_table:
%assign i 0
%rep 256
    dq isr%+i
%assign i i+1
%endrep
