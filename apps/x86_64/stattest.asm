bits 64
default rel
%include "user_abi.inc"
global _start
%macro callgate 1
    mov eax, %1
    int USER_GATE
%endmacro
%macro expect 1
    mov r15, __LINE__
    cmp rax, %1
    jne bad
%endmacro
%macro statpath 2
    lea rdi, [%1]
    lea rsi, [%2]
    callgate USER_STAT
%endmacro
%macro fstatfd 2
    mov rdi, %1
    lea rsi, [%2]
    callgate USER_FSTAT
%endmacro
%macro badfd 1
    fstatfd %1, second
    expect -USER_EBADF
%endmacro
%macro badoutput 1
    lea rdi, [path_a]
    mov rsi, %1
    callgate USER_STAT
    expect -USER_EFAULT
    mov edi, 3
    mov rsi, %1
    callgate USER_FSTAT
    expect -USER_EFAULT
%endmacro
%macro badpath 2
    mov rdi, %1
    lea rsi, [second]
    callgate USER_STAT
    expect %2
%endmacro
section .text
_start:
    lea r14, [path_a]
    cmp qword [mode], 2
    jne .path
    lea r14, [path_b]
.path:
    ; Poison both outputs: compare all bytes, including zeroed reserved fields.
    lea rdi, [first]
    mov ecx, 16
    mov rax, -1
    rep stosq
    mov rdi, r14
    lea rsi, [first]
    callgate USER_STAT
    expect 0
    cmp dword [first], USER_STAT_VERSION
    jne bad
    cmp dword [first+4], USER_STAT_SIZE
    jne bad
    cmp dword [first+8], USER_TYPE_REGULAR
    jne bad
    cmp dword [first+12], USER_TIME_POLLIK_TICKS
    jne bad
    cmp qword [first+16], 16
    jne bad
    cmp qword [first+24], 0
    je bad
    mov eax, 0x80000001
    cmp [first+32], rax
    jne bad
    mov eax, 0xfedcba98
    cmp [first+40], rax
    jne bad
    cmp qword [first+48], 0
    jne bad
    cmp qword [first+56], 0
    jne bad
    mov rdi, r14
    mov esi, USER_O_RDONLY
    callgate USER_OPEN
    expect 3
    ; Nonzero offset must survive fstat and stat.
    mov edi, 3
    mov esi, 5
    xor edx, edx
    callgate USER_SEEK
    expect 5
    call compare_metadata
    cmp qword [mode], 3
    je errors
    cmp qword [mode], 4
    je io_failure
    cmp qword [mode], 5
    jae termination
    cmp qword [mode], 0
    je normal
    mov qword [phase], 1
.wait:
    call compare_metadata
    cmp qword [release], 0
    je .wait
    mov edi, 3
    callgate USER_CLOSE
    expect 0
    badfd 3
    ; Path queries work with no descriptor, even while peer fd 3 is open.
    mov rdi, r14
    lea rsi, [second]
    callgate USER_STAT
    expect 0
    call compare_bytes
    mov qword [phase], 2
.closed:
    cmp qword [release], 2
    jb .closed
    jmp done
normal:
    statpath directory, second
    expect 0
    cmp dword [second+8], USER_TYPE_DIRECTORY
    jne bad
    cmp qword [second+16], 1024
    jne bad
    statpath root, second
    expect 0
    cmp dword [second+8], USER_TYPE_DIRECTORY
    jne bad
    ; Successful copy crossing two mapped writable pages.
    statpath path_a, cross_page
    expect 0
    lea rsi, [first]
    lea rdi, [cross_page]
    mov ecx, USER_STAT_SIZE
    repe cmpsb
    jne bad
close_done:
    mov edi, 3
    callgate USER_CLOSE
    expect 0
    jmp done
compare_metadata:
    fstatfd 3, second
    expect 0
    call compare_bytes
    mov edi, 3
    xor esi, esi
    mov edx, USER_SEEK_CUR
    callgate USER_SEEK
    expect 5
    ret
compare_bytes:
    lea rsi, [first]
    lea rdi, [second]
    mov ecx, USER_STAT_SIZE
    repe cmpsb
    jne bad
    ret
errors:
    ; C1 gives 0..2 stream semantics; close them before closed-fd regressions.
    mov edi, 0
    callgate USER_CLOSE
    expect 0
    mov edi, 1
    callgate USER_CLOSE
    expect 0
    mov edi, 2
    callgate USER_CLOSE
    expect 0
    badfd -1
    badfd 0
    badfd 1
    badfd 2
    badfd 4
    badfd 15
    badfd 16
    badfd 0x100000003
    badoutput 0xffffff8000000000
    badoutput 0x0000800000000000
    badoutput USER_PRIVATE
    badoutput _start
    badoutput bss_end-32
    badoutput 0xffffffffffffffe0
    lea rdi, [second]
    mov ecx, USER_STAT_SIZE
    mov al, 0xa5
    rep stosb
    mov qword [bss_end-32], 0x12345678
    badoutput bss_end-32
    cmp qword [bss_end-32], 0x12345678
    jne bad
    badpath 0xffffff8000000000, -USER_EFAULT
    badpath 0x0000800000000000, -USER_EFAULT
    badpath USER_PRIVATE, -USER_EFAULT
    mov byte [bss_end-1], 'x'
    badpath bss_end-1, -USER_EFAULT
    badpath long_path, -USER_ENAMETOOLONG
    badpath missing, -USER_ENOENT
    badpath parent_path, -USER_ENOENT
    badpath empty_path, -USER_EINVAL
    ; Errors leave the entire output untouched.
    lea rdi, [second]
    mov ecx, USER_STAT_SIZE
    mov al, 0xa5
    repe scasb
    jne bad
    call compare_metadata
    mov edi, 3
    callgate USER_CLOSE
    expect 0
    badfd 3
    jmp done
io_failure:
    mov qword [phase], 1
.wait:
    cmp qword [release], 0
    je .wait
    mov r12d, 20
.again:
    lea rdi, [second]
    mov ecx, USER_STAT_SIZE
    mov al, 0xa5
    rep stosb
    statpath path_a, second
    expect -USER_EIO
    fstatfd 3, second
    expect -USER_EIO
    lea rdi, [second]
    mov ecx, USER_STAT_SIZE
    mov al, 0xa5
    repe scasb
    jne bad
    mov edi, 3
    xor esi, esi
    mov edx, USER_SEEK_CUR
    callgate USER_SEEK
    expect 5
    dec r12
    jnz .again
    jmp close_done
termination:
    mov qword [phase], 1
    cmp qword [mode], 5
    je done
    cmp qword [mode], 6
    jne .spin
    mov byte [_start], 0
.spin:
    jmp .spin
done:
    mov edi, 42
    jmp exit
bad:
    mov [failure_rax], rax
    mov edi, 255
exit:
    callgate USER_EXIT
    ud2
section .rodata
path_a: db '/etc/read_test.txt',0
path_b: db '/etc/other.txt',0
directory: db '/etc',0
root: db '/',0
missing: db '/etc/missing',0
relative_path: db 'etc/read_test.txt',0
parent_path: db '/etc/../read_test.txt',0
empty_path: db 0
long_path: times USER_PATH_MAX db 'x'
    db 0
section .data
mode: dq 0
phase: dq 0
release: dq 0
failure_rax: dq 0
section .bss align=4096
first: resb USER_STAT_SIZE
second: resb USER_STAT_SIZE
    resb 4096-128-32
cross_page: resb 64
    resb 4096-32
bss_end:
