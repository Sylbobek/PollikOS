bits 64
default rel
%include "user_abi.inc"
global _start
section .text
_start:
    sub rsp, 16
    mov rax, [identity]
    mov [rsp], rax
    not rax
    mov [rsp+8], rax
    mov [stack_address], rsp
    mov rax, 0x101
    mov rcx, 0x102
    mov rdx, 0x103
    mov rbx, 0x104
    mov rbp, 0x105
    mov rsi, 0x106
    mov rdi, 0x107
    mov r8,  0x108
    mov r9,  0x109
    mov r10, 0x10a
    mov r11, 0x10b
    mov r12, 0x10c
    mov r13, 0x10d
    mov r14, 0x10e
    mov r15, 0x10f
    std
    stc
.loop:
    pushfq
    test qword [rsp], 1
    jz bad
    test qword [rsp], 0x400
    jz bad
    test qword [rsp], 0x200
    jz bad
    add rsp, 8
%macro verify 2
    cmp %1, %2
    jne bad
%endmacro
    verify rax, 0x101
    verify rcx, 0x102
    verify rdx, 0x103
    verify rbx, 0x104
    verify rbp, 0x105
    verify rsi, 0x106
    verify rdi, 0x107
    verify r8,  0x108
    verify r9,  0x109
    verify r10, 0x10a
    verify r11, 0x10b
    verify r12, 0x10c
    verify r13, 0x10d
    verify r14, 0x10e
    verify r15, 0x10f
    cmp rsp, [stack_address]
    jne bad
    push rax
    mov rax, [identity]
    cmp [rsp+8], rax
    jne bad
    not rax
    cmp [rsp+16], rax
    jne bad
    pop rax
    inc qword [progress]
    inc qword [verified]
    cmp qword [command], 1
    je done
    cmp qword [command], 2
    je text_fault
    cmp qword [command], 3
    je nx_fault
    cmp qword [command], 4
    je kernel_fault
    cmp qword [command], 5
    je gp_fault
    cmp qword [command], 6
    je ud_fault
    cmp qword [command], 7
    je fpu_fault
    cmp qword [print_requested], 0
    je .continue
    ; Only syscall-result RAX and argument registers are changed by this macro.
    push rax
    push rdi
    push rsi
    mov qword [print_requested], 0
    push rdx
    mov eax, USER_WRITE
    mov edi, 1
    lea rsi, [identity]
    mov edx, 1
    int USER_GATE
    cmp rax, 1
    jne bad
    pop rdx
    pop rsi
    pop rdi
    pop rax
.continue:
    std
    stc
    jmp .loop                 ; no cooperative yield and no voluntary exit
done:
    mov edi, 42
    jmp exit
bad:
    mov edi, 255
exit:
    cld
    mov eax, USER_EXIT
    int USER_GATE
    ud2
text_fault:
    mov byte [_start], 0
    ud2
nx_fault:
    jmp command
kernel_fault:
    mov al, [abs 0x100000]
    ud2
gp_fault:
    cli
    ud2
ud_fault:
    ud2
fpu_fault:
    fninit
    fld qword [fpu_half]
    mov rdx, 0x3fe0000000000000 ; 0.5 in the low XMM lane
    movq xmm0, rdx
    mov eax, USER_SLEEP
    mov edi, 10
    int USER_GATE                 ; block while another process uses its own FPU state
    movq rdx, xmm0
    mov rbx, 0x3fe0000000000000
    cmp rdx, rbx
    jne bad
    fstp qword [rsp]
    cmp qword [rsp], rbx
    jne bad
    mov edi, 42
    mov eax, USER_EXIT
    int USER_GATE
    ud2
section .data
%ifdef FAULT_TEST
command: dq 2
%else
command: dq 0
%endif
identity: dq 'A'
progress: dq 0
verified: dq 0
print_requested: dq 1
stack_address: dq 0
fpu_half: dq 0x3fe0000000000000
