bits 64
default rel
%include "user_abi.inc"
global _start
%macro callgate 1
    mov eax, %1
    int USER_GATE
%endmacro
%macro expect 1
    mov r15, %1
    cmp rax, %1
    jne bad
%endmacro
%macro openpath 1
    lea rdi, [%1]
    mov esi, USER_O_RDONLY
    callgate USER_OPEN
%endmacro
%macro readinto 2
    mov edi, 3
    lea rsi, [%1]
    mov edx, %2
    callgate USER_READ
%endmacro
%macro seekto 2
    mov edi, 3
    mov rsi, %1
    mov edx, %2
    callgate USER_SEEK
%endmacro
%macro invalid_fd 1
    mov rdi, %1
    lea rsi, [buffer]
    mov edx, 1
    callgate USER_READ
    expect -USER_EBADF
    mov rdi, %1
    xor esi, esi
    xor edx, edx
    callgate USER_SEEK
    expect -USER_EBADF
    mov rdi, %1
    callgate USER_CLOSE
    expect -USER_EBADF
%endmacro
%macro bad_read 2
    mov edi, 3
    mov rsi, %1
    mov edx, %2
    callgate USER_READ
    expect -USER_EFAULT
%endmacro
section .text
_start:
    cmp qword [mode], 9
    je bulk_hold
    cmp qword [mode], 7
    je allocation_failure
    cmp qword [mode], 6
    je errors
    cmp qword [mode], 3
    jb normal_or_isolation
    cmp qword [mode], 5
    jbe termination
    cmp qword [mode], 8
    je read_failure
    jmp bad
normal_or_isolation:
    cmp qword [mode], 2
    je .other
    openpath path_a
    jmp .opened
.other:
    openpath path_b
.opened:
    expect 3
    mov [opened_fd], rax
    cmp qword [mode], 0
    je normal
    readinto buffer, 1
    expect 1
    cmp qword [mode], 1
    jne .b
    cmp byte [buffer], 'A'
    jne bad
    mov qword [phase], 1
.wait_a:
    cmp qword [release], 1
    jb .wait_a
    mov edi, 3
    callgate USER_CLOSE
    expect 0
    mov qword [phase], 2
.wait_closed:
    cmp qword [release], 2
    jb .wait_closed
    jmp done
.b:
    cmp byte [buffer], 'B'
    jne bad
    ; fd 4 exists in neither table. Numeric values never name peer handles.
    invalid_fd 4
    mov qword [phase], 1
.wait_b:
    cmp qword [release], 1
    jb .wait_b
    seekto 0, USER_SEEK_CUR
    expect 1
    readinto buffer, 1
    expect 1
    cmp byte [buffer], 'r'
    jne bad
    mov edi, 3
    callgate USER_CLOSE
    expect 0
    mov qword [phase], 2
    jmp done
normal:
    lea rdi, [buffer]
    mov ecx, USER_READ_MAX
    mov al, 0xa5
    rep stosb
    readinto buffer, USER_READ_MAX
    expect 16
    lea rsi, [buffer]
    lea rdi, [expected_a]
    mov ecx, 16
    repe cmpsb
    jne bad
    cmp byte [buffer+16], 0xa5
    jne bad
    readinto buffer+32, 8
    expect 0
    cmp byte [buffer+32], 0xa5
    jne bad
    seekto 0, USER_SEEK_SET
    expect 0
    ; Successful copy spans two writable pages.
    readinto buffer+4090, 16
    expect 16
    lea rsi, [buffer+4090]
    lea rdi, [expected_a]
    mov ecx, 16
    repe cmpsb
    jne bad
    seekto -4, USER_SEEK_END
    expect 12
    readinto buffer, 4
    expect 4
    cmp dword [buffer], 0x0a617461 ; "ata\n"
    jne bad
    seekto -2, USER_SEEK_CUR
    expect 14
    readinto buffer, 2
    expect 2
    cmp word [buffer], 0x0a61
    jne bad
    seekto 32, USER_SEEK_SET
    expect 32
    readinto buffer, 8
    expect 0
    mov edi, 3
    callgate USER_CLOSE
    expect 0
    openpath path_a
    expect 3
    seekto 0, USER_SEEK_CUR
    expect 0
    mov edi, 3
    callgate USER_CLOSE
    expect 0
    jmp done
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
    invalid_fd -1
    invalid_fd 0
    invalid_fd 1
    invalid_fd 2
    invalid_fd 3
    invalid_fd 15
    invalid_fd 16
    invalid_fd 0x100000003
    ; C5 open-flag contract: access bits are required, write modes are valid
    ; for regular files, and unknown/overflowing bits are rejected.
    lea rdi, [path_a]
    mov esi, 0
    callgate USER_OPEN
    expect -USER_EINVAL
    lea rdi, [path_a]
    mov esi, 0x100
    callgate USER_OPEN
    expect -USER_EINVAL
    lea rdi, [path_a]
    mov esi, 0x200
    callgate USER_OPEN
    expect -USER_EINVAL
    lea rdi, [path_a]
    mov esi, 0x400
    callgate USER_OPEN
    expect -USER_EINVAL
    lea rdi, [path_a]
    mov rsi, 0x100000001
    callgate USER_OPEN
    expect -USER_EINVAL
    lea rdi, [path_a]
    mov esi, USER_O_WRONLY
    callgate USER_OPEN
    expect 3
    mov edi, 3
    callgate USER_CLOSE
    expect 0
    lea rdi, [path_a]
    mov esi, USER_O_RDWR
    callgate USER_OPEN
    expect 3
    mov edi, 3
    callgate USER_CLOSE
    expect 0
    ; Read-only descriptors still reject writes and append/trunc are refused
    ; without write access.
    lea rdi, [path_a]
    mov esi, USER_O_RDONLY|USER_O_TRUNC
    callgate USER_OPEN
    expect -USER_EACCES
    lea rdi, [path_a]
    mov esi, USER_O_RDONLY|USER_O_APPEND
    callgate USER_OPEN
    expect -USER_EACCES
    openpath missing
    expect -USER_ENOENT
    openpath directory
    expect -USER_EISDIR
    openpath relative_path
    expect 3
    mov edi, 3
    callgate USER_CLOSE
    expect 0
    openpath parent_path
    expect -USER_ENOENT
    mov rdi, 0x100000
    mov esi, USER_O_RDONLY
    callgate USER_OPEN
    expect -USER_EFAULT
    mov rdi, USER_LIMIT
    callgate USER_OPEN
    expect -USER_EFAULT
    mov rdi, USER_PRIVATE
    callgate USER_OPEN
    expect -USER_EFAULT
    openpath long_path
    expect -USER_ENAMETOOLONG
    lea rdi, [bss_end-128]
    mov ecx, 128
    mov al, 'x'
    rep stosb
    lea rdi, [bss_end-128]
    mov esi, USER_O_RDONLY
    callgate USER_OPEN
    expect -USER_ENAMETOOLONG
    openpath path_a
    expect 3
    bad_read 0x100000, 8
    bad_read USER_LIMIT, 8
    bad_read USER_PRIVATE, 8
    bad_read bss_end-4, 8
    cmp dword [bss_end-4], 0x78787878
    jne bad
    bad_read _start, 8
    bad_read 0xfffffffffffffff8, 16
    seekto 0, USER_SEEK_CUR
    expect 0
    mov edi, 3
    mov rsi, 0xffffffffffffffff
    xor edx, edx
    callgate USER_READ
    expect 0
    readinto buffer, USER_READ_MAX+1
    expect -USER_E2BIG
    mov edi, 3
    lea rsi, [buffer]
    mov rdx, -1
    callgate USER_READ
    expect -USER_E2BIG
    seekto -1, USER_SEEK_SET
    expect -USER_EINVAL
    seekto 0x7fffffffffffffff, USER_SEEK_SET
    expect -USER_EINVAL
    seekto 0x8000000000000000, USER_SEEK_CUR
    expect -USER_EINVAL
    seekto 0, 3
    expect -USER_EINVAL
    mov edi, 3
    xor esi, esi
    mov rdx, 0x100000000
    callgate USER_SEEK
    expect -USER_EINVAL
    seekto 0x7fffffff, USER_SEEK_SET
    expect 0x7fffffff
    seekto 1, USER_SEEK_CUR
    expect -USER_EINVAL
    seekto 0, USER_SEEK_CUR
    expect 0x7fffffff
    seekto 0, USER_SEEK_END
    expect 16
    mov edi, 3
    callgate USER_CLOSE
    expect 0
    invalid_fd 3
    call fill_table
    openpath path_a
    expect -USER_EMFILE
    mov edi, 7
    callgate USER_CLOSE
    expect 0
    openpath path_b
    expect 7
    mov r12d, 3
.close_all:
    mov rdi, r12
    callgate USER_CLOSE
    expect 0
    inc r12
    cmp r12, USER_FD_LIMIT
    jb .close_all
    invalid_fd 3
    jmp done
fill_table:
    mov r12d, 3
.open:
    openpath path_a
    cmp rax, r12
    jne bad
    inc r12
    cmp r12, USER_FD_LIMIT
    jb .open
    ret
bulk_hold:
    call fill_table
    mov qword [phase], 1
.bulk_wait:
    cmp qword [release], 1
    jb .bulk_wait
    jmp done
termination:
    call fill_table
    mov qword [phase], 1
    cmp qword [mode], 3
    je done
    cmp qword [mode], 4
    je .fault
.spin:
    jmp .spin
.fault:
    mov byte [_start], 0
    jmp bad
allocation_failure:
    mov r12d, 20
.again:
    openpath path_a
    expect -USER_ENOMEM
    dec r12
    jnz .again
    jmp done
read_failure:
    openpath path_a
    expect 3
    mov qword [phase], 1
.wait:
    cmp qword [release], 1
    jb .wait
    mov r12d, 20
.again:
    mov qword [buffer], 0x12345678
    readinto buffer, 8
    expect -USER_EIO
    cmp qword [buffer], 0x12345678
    jne bad
    seekto 0, USER_SEEK_CUR
    expect 0
    dec r12
    jnz .again
    mov edi, 3
    callgate USER_CLOSE
    expect 0
    jmp done
done:
    mov edi, 42
    jmp exit
bad:
    mov [failure_rax], rax
    mov edi, 255
exit:
    mov eax, USER_EXIT
    int USER_GATE
    ud2
section .rodata
path_a: db '/etc/read_test.txt',0
path_b: db '/etc/other.txt',0
expected_a: db 'Alpha file data',10
missing: db '/etc/missing',0
directory: db '/etc',0
relative_path: db 'etc/read_test.txt',0
parent_path: db '/etc/../read_test.txt',0
long_path: times 128 db 'x'
    db 0
section .data
mode: dq 0
phase: dq 0
release: dq 0
opened_fd: dq 0
failure_rax: dq 0
section .bss align=4096
buffer: resb 8192
    resb 4096
bss_end:
