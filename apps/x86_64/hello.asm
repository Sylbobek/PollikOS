bits 64
default rel
%include "user_abi.inc"
; PollikOS native SYSCALL transport; RCX/R11 are architectural clobbers.
%macro stdout_write 2
    mov edi, 1
    lea rsi, [rel %1]
    mov edx, %2
    mov eax, USER_WRITE
    syscall
%endmacro
%macro exit_status 1
    mov edi, %1
    mov eax, USER_EXIT
    syscall
    ud2
%endmacro
section .text
global _start
_start:
    test rsp, 15
    jnz bad
    cmp rcx, STARTUP_VERSION
    jne bad
    cmp qword [rsp], STARTUP_VERSION
    jne bad
    cmp rdi, 3
    jne bad
    cmp [rsp+8], rdi
    jne bad
    cmp [rsp+16], rsi
    jne bad
    cmp qword [rsp+24], 1
    jne bad
    cmp [rsp+32], rdx
    jne bad
    cmp qword [rsi+24], 0
    jne bad
    cmp qword [rdx+8], 0
    jne bad
    mov rdi, [rsi]
    lea r8, [rel arg0]
    call equal
    mov rdi, [rsi+8]
    lea r8, [rel arg1]
    call equal
    mov rdi, [rsi+16]
    lea r8, [rel arg2]
    call equal
    mov rdi, [rdx]
    lea r8, [rel env0]
    call equal
    mov rax, 0x123456789abcdef0
    cmp [rel initialized], rax
    jne bad
    lea rdi, [rel zeroed]
    mov ecx, 8193
.bss:
    cmp byte [rdi], 0
    jne bad
    inc rdi
    loop .bss
    mov byte [rel zeroed], 77 ; private BSS must be writable
    cmp qword [rel mode], 1
    je text_fault
    cmp qword [rel mode], 2
    je nx_fault
    cmp qword [rel mode], 3
    je kernel_fault
    stdout_write message, message_end-message
    cmp rax, message_end-message
    jne bad
    exit_status 42
equal:
    mov al, [rdi]
    cmp al, [r8]
    jne bad
    inc rdi
    inc r8
    test al, al
    jnz equal
    ret
text_fault:
    mov byte [rel _start], 0
    jmp bad
nx_fault:
    lea rax, [rel zeroed]
    jmp rax
kernel_fault:
    mov eax, 0x100000
    mov rax, [rax]
bad:
    exit_status 255
section .rodata
%ifdef ARGV_TEST
message: db 'ARGV ENVP from PollikFS verified',10
%else
message: db 'Hello from ELF64 PollikOS',10
%endif
message_end:
arg0: db 'hello.elf',0
arg1: db 'first',0
arg2: db 'second',0
env0: db 'TEST=pollikos',0
section .data
global mode
mode: dq 0
initialized: dq 0x123456789abcdef0
section .bss
zeroed: resb 8193
