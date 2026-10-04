bits 64
default rel
%include "user_abi.inc"
global _start
%macro gate 1
    mov eax, %1
    syscall
%endmacro
%macro expect 1
    mov r15, __LINE__
    cmp rax, %1
    jne bad
%endmacro
%macro chdir 1
    lea rdi, [%1]
    gate USER_CHDIR
%endmacro
%macro open 1
    lea rdi, [%1]
    mov esi, USER_O_RDONLY
    gate USER_OPEN
%endmacro
%macro cwd 2
    lea rdi, [buffer]
    mov esi, USER_PATH_MAX
    gate USER_GETCWD
    expect %2
    lea rdi, [buffer]
    lea rsi, [%1]
    mov ecx, %2+1
    repe cmpsb
    jne bad
%endmacro
%macro write 3
    mov rdi, %1
    lea rsi, [%2]
    mov edx, %3
    gate USER_WRITE
%endmacro
%macro badwrite 3
    mov edi, 1
    mov rsi, %1
    mov rdx, %2
    gate USER_WRITE
    expect %3
%endmacro
section .text
_start:
    cmp qword [mode], 7
    jae bad_return
    lea rdi, [abi_info]
    mov dword [abi_info], 24
    gate USER_ABI_INFO
    expect 24
    cmp dword [abi_info], 24
    jne bad
    cmp word [abi_info+4], 1
    jne bad
    cmp word [abi_info+6], 0
    jne bad
    cmp dword [abi_info+8], 2
    jne bad
    cmp dword [abi_info+12], 2
    jne bad
    cmp dword [abi_info+16], 2
    jne bad
    cmp dword [abi_info+20], 27
    jne bad
    mov rdi, 0xffffff8000000000
    gate USER_ABI_INFO
    expect -USER_EFAULT
    mov dword [short_abi_size], 7
    lea rdi, [short_abi_size]
    gate USER_ABI_INFO
    expect -USER_EINVAL
    cmp dword [short_abi_size], 7
    jne bad
    cwd root, 1
    chdir bin_slashes
    expect 0
    cwd bin, 4
    open hello
    expect 3
    lea rdi, [hello]
    lea rsi, [metadata]
    gate USER_STAT
    expect 0
    cmp dword [metadata+8], USER_TYPE_REGULAR
    jne bad
    mov edi, 3
    gate USER_CLOSE
    expect 0
    lea rdi, [absolute_hello]
    lea rsi, [metadata]
    gate USER_STAT
    expect 0
    chdir rooted_bin
    expect 0
    cwd bin, 4
    chdir etc_relative
    expect 0
    cwd etc, 4
    open file_relative
    expect 3
    mov edi, 3
    lea rsi, [buffer]
    mov edx, 16
    gate USER_READ
    expect 16
    lea rdi, [buffer]
    lea rsi, [content]
    mov ecx, 16
    repe cmpsb
    jne bad
    mov edi, 3
    gate USER_CLOSE
    expect 0
    chdir root_parents
    expect 0
    cwd root, 1
    chdir test_directory
    expect 0
    chdir dot
    expect 0
    cwd test_directory, 8
    lea rdi, [dot]
    mov esi, USER_O_RDONLY
    gate USER_OPENDIR
    expect 3
    mov edi, 3
    lea rsi, [entry]
    gate USER_READDIR
    expect 1
    cmp dword [entry+12], 9
    jne bad
    mov edi, 3
    gate USER_CLOSE
    expect 0
    ; .. after subdir and a trailing slash normalize deterministically.
    chdir subdir_parent
    expect 0
    cwd test_directory, 8
    chdir missing
    expect -USER_ENOENT
    chdir regular_absolute
    expect -USER_ENOTDIR
    cwd test_directory, 8
    chdir parent
    expect 0
    cwd root, 1
    cmp qword [mode], 3
    je errors
    cmp qword [mode], 4
    jae termination
    cmp qword [mode], 0
    jne peers
    write 1, stdout_message, stdout_end-stdout_message
    expect stdout_end-stdout_message
    write 2, stderr_message, stderr_end-stderr_message
    expect stderr_end-stderr_message
    call exercise
    jmp done
peers:
    cmp qword [mode], 1
    jne .b
    chdir bin
    expect 0
    open hello
    expect 3
    jmp .ready
.b:
    chdir etc
    expect 0
    open file_name
    expect 3
.ready:
    call exercise
    mov qword [phase], 1
.spin:
    call exercise
    cmp qword [release], 1
    jb .spin
    cmp qword [mode], 1
    jne .verify_b
    cwd bin, 4
    mov edi, 1
    gate USER_CLOSE
    expect 0
    mov edi, 3
    gate USER_CLOSE
    expect 0
    mov qword [phase], 2
.wait:
    cmp qword [release], 2
    jb .wait
    jmp done
.verify_b:
    cwd etc, 4
    mov qword [phase], 2
.wait_b:
    cmp qword [release], 2
    jb .wait_b
    write 1, peer_message, peer_end-peer_message
    expect peer_end-peer_message
    mov edi, 3
    gate USER_CLOSE
    expect 0
    jmp done
; Repeatedly verify every preserved GPR, RSP and DF/CF return state.
exercise:
    mov qword [remaining], 256
.again:
    mov rbx, 0x101
    mov rbp, 0x102
    mov r12, 0x103
    mov r13, 0x104
    mov r14, 0x105
    mov r15, 0x106
    mov rdi, 0x107
    mov rsi, 0x108
    mov rdx, 0x109
    mov r10, 0x10a
    mov r8, 0x10b
    mov r9, 0x10c
    mov [saved_rsp], rsp
    std
    stc
    gate 0x504fffff
.after:
    pushfq
    pop qword [returned_flags]
    cld
    cmp rax, -USER_ENOSYS
    jne bad
    lea rax, [.after]
    cmp rcx, rax
    jne bad
    cmp rsp, [saved_rsp]
    jne bad
    cmp rbx, 0x101
    jne bad
    cmp rbp, 0x102
    jne bad
    cmp r12, 0x103
    jne bad
    cmp r13, 0x104
    jne bad
    cmp r14, 0x105
    jne bad
    cmp r15, 0x106
    jne bad
    cmp rdi, 0x107
    jne bad
    cmp rsi, 0x108
    jne bad
    cmp rdx, 0x109
    jne bad
    cmp r10, 0x10a
    jne bad
    cmp r8, 0x10b
    jne bad
    cmp r9, 0x10c
    jne bad
    mov rax, [returned_flags]
    and eax, 0x401
    cmp eax, 0x401
    jne bad
    mov rax, r11
    and eax, 0x401
    cmp eax, 0x401
    jne bad
    inc qword [calls]
    dec qword [remaining]
    jnz .again
    ret
errors:
    write -1, stdout_message, 1
    expect -USER_EBADF
    write 0, stdout_message, 1
    expect -USER_EACCES
    write 16, stdout_message, 1
    expect -USER_EBADF
    write 0x100000001, stdout_message, 1
    expect -USER_EBADF
    open regular_absolute
    expect 3
    write 3, stdout_message, 1
    expect -USER_EACCES
    mov edi, 3
    gate USER_CLOSE
    expect 0
    write 3, stdout_message, 1
    expect -USER_EBADF
    badwrite 0xffffff8000000000, 1, -USER_EFAULT
    badwrite 0x0000800000000000, 1, -USER_EFAULT
    badwrite USER_PRIVATE, 1, -USER_EFAULT
    badwrite bss_end-1, 2, -USER_EFAULT
    badwrite 0xfffffffffffffff0, 32, -USER_EFAULT
    badwrite buffer, USER_WRITE_MAX+1, -USER_E2BIG
    badwrite 0xfffffffffffffff0, 0, 0
    mov edi, 0
    lea rsi, [buffer]
    mov edx, 1
    gate USER_READ
    expect -USER_ENOTSUP
    mov edi, 0
    xor esi, esi
    xor edx, edx
    gate USER_READ
    expect 0
    mov edi, 1
    lea rsi, [buffer]
    mov edx, 1
    gate USER_READ
    expect -USER_EACCES
    mov edi, 2
    gate USER_CLOSE
    expect 0
    write 2, stderr_message, 1
    expect -USER_EBADF
    mov edi, 2
    gate USER_CLOSE
    expect -USER_EBADF
    ; Large write crosses three mapped pages and the 4 KiB chunk boundary.
    lea rdi, [buffer]
    mov ecx, 8193
    mov al, 'W'
    rep stosb
    write 1, buffer, 8193
    expect 8193
    write 1, newline, 1
    expect 1
    ; Readable RX buffers were exercised by the normal stdout message.
    mov byte [buffer], 0x5a
    lea rdi, [buffer]
    mov esi, 1
    gate USER_GETCWD
    expect -USER_ERANGE
    cmp byte [buffer], 0x5a
    jne bad
    lea rdi, [buffer]
    xor esi, esi
    gate USER_GETCWD
    expect -USER_ERANGE
%macro badcwd 1
    mov rdi, %1
    mov esi, USER_PATH_MAX
    gate USER_GETCWD
    expect -USER_EFAULT
%endmacro
    badcwd 0xffffff8000000000
    badcwd 0x0000800000000000
    badcwd USER_PRIVATE
    badcwd _start
    badcwd bss_end-1
    badcwd 0xffffffffffffffff
%macro badchdir 2
    mov rdi, %1
    gate USER_CHDIR
    expect %2
%endmacro
    badchdir 0xffffff8000000000, -USER_EFAULT
    badchdir 0x0000800000000000, -USER_EFAULT
    badchdir USER_PRIVATE, -USER_EFAULT
    badchdir empty_path, -USER_EINVAL
    badchdir long_path, -USER_ENAMETOOLONG
    mov byte [bss_end-1], 'x'
    badchdir bss_end-1, -USER_EFAULT
    chdir missing
    expect -USER_ENOENT
    cwd root, 1
    ; Lexical composition exceeding the path cap fails without changing cwd.
    chdir test_directory
    expect 0
    badchdir composed_long, -USER_ENAMETOOLONG
    cwd test_directory, 8
    jmp done
termination:
    mov r14d, 3
.fill:
    open regular_absolute
    cmp rax, r14
    jne bad
    inc r14
    cmp r14, USER_FD_LIMIT
    jb .fill
    chdir etc
    expect 0
    mov qword [phase], 1
    cmp qword [mode], 4
    je done
    cmp qword [mode], 5
    jne .spin
    mov byte [_start], 0
.spin:
    gate 0x504fffff
    jmp .spin
bad_return:
    cmp qword [mode], 7
    jne .noncanonical
    mov rsp, 0xffffff8000001000
    jmp .call
.noncanonical:
    cmp qword [mode], 8
    jne .unmapped
    mov rsp, 0x0000800000000000
    jmp .call
.unmapped:
    mov rsp, USER_PRIVATE
.call:
    gate 0x504fffff
    ud2
done:
    mov edi, 42
    jmp exit
bad:
    mov [failure_rax], rax
    mov edi, 255
exit:
    gate USER_EXIT
    ud2
section .rodata
root: db '/',0
bin: db '/bin',0
bin_slashes: db '////bin///',0
etc: db '/etc',0
etc_relative: db '../etc',0
hello: db './hello',0
absolute_hello: db '////bin///hello',0
rooted_bin: db '/../../bin',0
file_name: db 'read_test.txt',0
file_relative: db './unused/../read_test.txt',0
regular_absolute: db '/etc/read_test.txt',0
root_parents: db '../../../../',0
test_directory: db '/testdir',0
subdir_parent: db 'subdir/../',0
dot: db '.',0
parent: db '..',0
missing: db '/missing',0
empty_path: db 0
long_path: times USER_PATH_MAX db 'x'
    db 0
composed_long:
    times 39 db 'a'
    db '/'
    times 39 db 'b'
    db '/'
    times 39 db 'c'
    db 0
content: db 'Alpha file data',10
stdout_message: db 'Hello from PollikOS stdout',10
stdout_end:
stderr_message: db 'Hello from PollikOS stderr',10
stderr_end:
peer_message: db '[C1] peer stdout remains open',10
peer_end:
newline: db 10
section .data
mode: dq 0
phase: dq 0
release: dq 0
failure_rax: dq 0
calls: dq 0
remaining: dq 0
saved_rsp: dq 0
returned_flags: dq 0
short_abi_size: dd 0
section .bss align=4096
buffer: resb 12288
metadata: resb 64
entry: resb 96
abi_info: resb 24
    resb 4096-184
bss_end:
