bits 64
default rel
%include "user_abi.inc"
section .rodata
global user_payload_start, user_payload_end
user_payload_start:
    ; Verify actual CPL and stack read/write before any kernel invocation.
    mov ax, cs
    and eax, 3
    cmp eax, 3
    jne bad
    mov rax, 0x123456789abcdef0
    push rax
    pop rbx
    cmp rax, rbx
    jne bad
    cmp r12, 1
    je kernel_read
    cmp r12, 2
    je readonly_write
    cmp r12, 3
    je nx_execute
    cmp r12, 4
    je unmapped_read
    cmp r12, 5
    je guard_read
    cmp r12, 6
    je private_read
    cmp r12, 7
    je copy_errors
    cmp r12, 8
    je bad_gate
    cmp r12, 9
    je user_privileged
    cmp r12, 10
    je quiet_exit
    cmp r12, 11
    je unsafe_return
    lea rdi, [rel message]
    mov esi, message_end-message
    mov eax, USER_DEBUG_WRITE
    int USER_GATE
    cmp rax, message_end-message
    jne bad
quiet_exit:
    mov rax, USER_DATA
    mov rdi, [rax]
    mov eax, USER_EXIT
    int USER_GATE
    ud2
kernel_read:
    mov rax, 0x100000
    mov rax, [rax]
    jmp bad
readonly_write:
    mov rax, USER_CODE
    mov byte [rax], 0
    jmp bad
nx_execute:
    mov rax, USER_DATA
    jmp rax
unmapped_read:
    xor eax, eax
    mov rax, [rax]
    jmp bad
guard_read:
    mov rax, USER_STACK_BASE
    mov rax, [rax]
    jmp bad
private_read:
    mov rax, USER_PRIVATE
    mov rax, [rax]
    jmp bad
copy_errors:
    ; Pointer checks in the real gate, and preserved high GPRs across IRETQ.
    mov rbx, 0x1122334455667788
    mov r13, 0x1122334455667788
    mov r14, 0x1122334455667788
    mov r15, 0x1122334455667788
    mov rdi, 0x100000
    mov esi, 8
    mov eax, USER_DEBUG_WRITE
    std
    int USER_GATE
    pushfq
    pop r10
    cld
    test r10, 0x400
    jz bad
    cmp rax, -USER_EFAULT
    jne bad
    cmp rbx, r13
    jne bad
    cmp r13, r14
    jne bad
    cmp r14, r15
    jne bad
    mov rdi, USER_LIMIT
    mov eax, USER_DEBUG_WRITE
    int USER_GATE
    cmp rax, -USER_EFAULT
    jne bad
    mov rdi, USER_DATA
    mov rsi, -1
    mov eax, USER_DEBUG_WRITE
    int USER_GATE
    cmp rax, -USER_E2BIG
    jne bad
    mov esi, 0
    mov eax, USER_DEBUG_WRITE
    int USER_GATE
    test rax, rax
    jnz bad
    xor eax, eax
    int USER_GATE
    cmp rax, -USER_ENOSYS
    jne bad
    jmp quiet_exit
bad_gate:
    int 0x80
    jmp bad
user_privileged:
    cli
    jmp bad
unsafe_return:
    mov rsp, USER_LIMIT
    mov rdi, USER_DATA
    xor esi, esi
    mov eax, USER_DEBUG_WRITE
    int USER_GATE
    jmp bad
bad:
    mov edi, 255
    mov eax, USER_EXIT
    int USER_GATE
    ud2
message: db 'Hello from PollikOS x86_64 Ring 3', 10
message_end:
user_payload_end:
