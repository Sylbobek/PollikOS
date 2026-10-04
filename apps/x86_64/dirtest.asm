bits 64
default rel
%include "user_abi.inc"
global _start
%macro gate 1
    mov eax, %1
    int USER_GATE
%endmacro
%macro expect 1
    mov r15, %1
    cmp rax, %1
    jne bad
%endmacro
%macro opendir 1
    lea rdi, [%1]
    mov esi, USER_O_RDONLY
    gate USER_OPENDIR
%endmacro
%macro badfd 1
    mov rdi, %1
    lea rsi, [entry]
    gate USER_READDIR
    expect -USER_EBADF
%endmacro
%macro badoutput 1
    mov edi, 3
    mov rsi, %1
    gate USER_READDIR
    expect -USER_EFAULT
%endmacro
%macro badpath 2
    mov rdi, %1
    mov esi, USER_O_RDONLY
    gate USER_OPENDIR
    expect %2
%endmacro
section .text
_start:
    cmp qword [mode], 8
    je allocation_failure
    opendir directory
    expect 3
    mov r13d, 3
    xor r12d, r12d
    ; Directory fstat succeeds; raw read and arbitrary byte seeks do not.
    mov edi, 3
    lea rsi, [metadata]
    gate USER_FSTAT
    expect 0
    cmp dword [metadata+8], USER_TYPE_DIRECTORY
    jne bad
    mov edi, 3
    lea rsi, [entry]
    mov edx, 1
    gate USER_READ
    expect -USER_EACCES
    mov edi, 3
    mov esi, 1
    xor edx, edx
    gate USER_SEEK
    expect -USER_EINVAL
    mov edi, 3
    xor esi, esi
    mov edx, USER_SEEK_CUR
    gate USER_SEEK
    expect -USER_EINVAL
    mov edi, 3
    xor esi, esi
    mov edx, USER_SEEK_END
    gate USER_SEEK
    expect -USER_EINVAL
    cmp qword [mode], 1
    je peer_a
    cmp qword [mode], 2
    je peer_b
    cmp qword [mode], 3
    je errors
    cmp qword [mode], 4
    je io_failure
    cmp qword [mode], 5
    jae termination
normal:
    call enumerate
    call eof
    call eof
    mov edi, 3
    xor esi, esi
    xor edx, edx
    gate USER_SEEK
    expect 0
    xor r12d, r12d
    call read_expected
    mov edi, 3
    gate USER_CLOSE
    expect 0
    badfd 3
    opendir empty_directory
    expect 3
    call eof
    call eof
    mov edi, 3
    gate USER_CLOSE
    expect 0
    jmp done
; Expected index r12, descriptor r13. The result straddles writable pages.
read_expected:
    lea rdi, [entry]
    mov ecx, USER_DIRENT_SIZE
    mov al, 0xa5
    rep stosb
    mov rdi, r13
    lea rsi, [entry]
    gate USER_READDIR
    expect 1
    mov r15, __LINE__
    cmp dword [entry], USER_DIRENT_VERSION
    jne bad
    cmp dword [entry+4], USER_DIRENT_SIZE
    jne bad
    mov eax, USER_TYPE_REGULAR
    cmp r12d, 2
    jne .type
    mov eax, USER_TYPE_DIRECTORY
.type:
    cmp [entry+8], eax
    jne bad
    lea rdx, [lengths]
    movzx eax, byte [rdx+r12]
    cmp [entry+12], eax
    jne bad
    cmp qword [entry+16], 0
    je bad
    cmp qword [entry+24], 0
    jne bad
    lea rsi, [names]
    mov rax, r12
    shl rax, 6
    add rsi, rax
    lea rdi, [entry+32]
    mov ecx, USER_DIRENT_NAME_CAPACITY
    repe cmpsb
    jne bad
    inc r12
    ret
enumerate:
.again:
    call read_expected
    cmp r12, 8
    jb .again
    ret
eof:
    lea rdi, [entry]
    mov ecx, USER_DIRENT_SIZE
    mov al, 0xa5
    rep stosb
    mov rdi, r13
    lea rsi, [entry]
    gate USER_READDIR
    expect 0
    lea rdi, [entry]
    mov ecx, USER_DIRENT_SIZE
    mov al, 0xa5
    repe scasb
    jne bad
    ret
peer_a:
    call read_expected
    mov qword [phase], 1
.wait:
    cmp qword [release], 1
    jb .wait
    call read_expected
    mov edi, 3
    gate USER_CLOSE
    expect 0
    badfd 3
    mov qword [phase], 2
.closed:
    cmp qword [release], 2
    jb .closed
    jmp done
peer_b:
    mov qword [phase], 1
.wait:
    cmp qword [release], 1
    jb .wait
    call read_expected
    mov qword [phase], 2
.first:
    cmp qword [release], 2
    jb .first
    call enumerate
    call eof
    mov edi, 3
    gate USER_CLOSE
    expect 0
    jmp done
errors:
    ; C1 gives 0..2 stream semantics; close them before closed-fd regressions.
    mov edi, 0
    gate USER_CLOSE
    expect 0
    mov edi, 1
    gate USER_CLOSE
    expect 0
    mov edi, 2
    gate USER_CLOSE
    expect 0
    badfd -1
    badfd 0
    badfd 1
    badfd 2
    badfd 4
    badfd 15
    badfd 16
    badfd 0x100000003
    lea rdi, [regular_file]
    mov esi, USER_O_RDONLY
    gate USER_OPEN
    expect 4
    mov edi, 4
    lea rsi, [entry]
    gate USER_READDIR
    expect -USER_ENOTDIR
    mov edi, 4
    gate USER_CLOSE
    expect 0
    badfd 4
    badoutput 0xffffff8000000000
    badoutput 0x0000800000000000
    badoutput USER_PRIVATE
    badoutput _start
    badoutput 0xffffffffffffffd0
    lea rdi, [bss_end-48]
    mov ecx, 48
    mov al, 0xa5
    rep stosb
    badoutput bss_end-48
    lea rdi, [bss_end-48]
    mov ecx, 48
    mov al, 0xa5
    repe scasb
    jne bad
    badpath missing, -USER_ENOENT
    badpath regular_file, -USER_ENOTDIR
    opendir relative_path
    expect 4
    mov edi, 4
    gate USER_CLOSE
    expect 0
    opendir parent_path
    expect 4
    mov edi, 4
    gate USER_CLOSE
    expect 0
    badpath 0xffffff8000000000, -USER_EFAULT
    badpath 0x0000800000000000, -USER_EFAULT
    badpath USER_PRIVATE, -USER_EFAULT
    badpath long_path, -USER_ENAMETOOLONG
    mov byte [bss_end-1], 'x'
    badpath bss_end-1, -USER_EFAULT
    lea rdi, [directory]
    xor esi, esi
    gate USER_OPENDIR
    expect -USER_EINVAL
    lea rdi, [directory]
    mov esi, 0x301
    gate USER_OPENDIR
    expect -USER_EISDIR
    lea rdi, [directory]
    mov esi, USER_O_RDONLY
    gate USER_OPEN
    expect -USER_EISDIR
    ; Rejected operations never consume an entry.
    call read_expected
    call fill
    opendir directory
    expect -USER_EMFILE
    mov edi, 7
    gate USER_CLOSE
    expect 0
    opendir directory
    expect 7
    mov r13d, 7
    xor r12d, r12d
    call read_expected
    mov r14d, 3
.close:
    mov rdi, r14
    gate USER_CLOSE
    expect 0
    inc r14
    cmp r14, USER_FD_LIMIT
    jb .close
    badfd 3
    jmp done
fill:
    mov r14d, 4
.again:
    opendir directory
    mov r15, __LINE__
    cmp rax, r14
    jne bad
    inc r14
    cmp r14, USER_FD_LIMIT
    jb .again
    ret
termination:
    call read_expected
    call fill
    mov qword [phase], 1
    cmp qword [mode], 5
    je done
    cmp qword [mode], 6
    jne .spin
    mov byte [_start], 0
.spin:
    jmp .spin
io_failure:
    mov qword [phase], 1
.wait:
    cmp qword [release], 1
    jb .wait
    mov r14d, 20
.again:
    lea rdi, [entry]
    mov ecx, USER_DIRENT_SIZE
    mov al, 0xa5
    rep stosb
    mov edi, 3
    lea rsi, [entry]
    gate USER_READDIR
    expect -USER_EIO
    lea rdi, [entry]
    mov ecx, USER_DIRENT_SIZE
    mov al, 0xa5
    repe scasb
    jne bad
    opendir directory
    expect -USER_EIO
    dec r14
    jnz .again
    mov qword [phase], 2
.recover:
    cmp qword [release], 2
    jb .recover
    call enumerate
    call eof
    mov edi, 3
    gate USER_CLOSE
    expect 0
    jmp done
allocation_failure:
    mov r14d, 20
.again:
    opendir directory
    expect -USER_ENOMEM
    dec r14
    jnz .again
    jmp done
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
directory: db '/testdir',0
empty_directory: db '/testdir/subdir',0
regular_file: db '/etc/read_test.txt',0
missing: db '/missing-directory',0
relative_path: db 'testdir',0
parent_path: db '/testdir/../testdir',0
long_path: times USER_PATH_MAX db 'x'
    db 0
lengths: db 9,8,6,9,7,10,55,9
%macro name 1
%%start: db %1,0
    times 64-($-%%start) db 0
%endmacro
names:
    name 'alpha.txt'
    name 'beta.txt'
    name 'subdir'
    name 'empty.txt'
    name '.layout'
    name '.trashinfo'
    times 55 db 'n'
    times 9 db 0
    name 'omega.txt'
section .data
mode: dq 0
phase: dq 0
release: dq 0
failure_rax: dq 0
section .bss align=4096
metadata: resb 64
    resb 4096-64-48
entry: resb USER_DIRENT_SIZE
    resb 4096-48
bss_end:
