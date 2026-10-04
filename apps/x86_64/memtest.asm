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
section .text
; C2 userspace memory fixture. Modes are selected by the kernel test through
; USER_DATA; all operations are real SYSCALL, no INT 0x81 and no floating point.
;   0: heap grow/write/shrink/regrow, zeroing, mmap/munmap, getpid, clock, sleep
;   1: test-only bump allocator built on brk
;   2: invalid brk requests
;   3: invalid mmap/munmap requests and mapping-record ownership
;   4: execute from a heap page (must fault: heap is NX)
;   6/7: peer heaps with identical virtual addresses across preemption
;   9: kernel-injected heap allocation failure, rollback and recovery
_start:
    mov rax, [mode]
    cmp rax, 1
    je bump_mode
    cmp rax, 2
    je bad_brk
    cmp rax, 3
    je bad_map
    cmp rax, 4
    je exec_heap
    cmp rax, 6
    je peer_mode
    cmp rax, 7
    je peer_mode
    cmp rax, 9
    je inject_mode
    jmp basic
; ---------------------------------------------------------------------------
basic:
    mov rbx, USER_HEAP_BASE
    xor edi, edi
    gate USER_BRK
    cmp rax, rbx
    jne bad
    mov rdi, USER_HEAP_BASE+16384
    gate USER_BRK
    mov rcx, USER_HEAP_BASE+16384
    cmp rax, rcx
    jne bad
    xor rcx, rcx
.fill:
    mov rax, 0x1122334455667788
    add rax, rcx
    mov [rbx+rcx], rax
    add rcx, 8
    cmp rcx, 16384
    jb .fill
    xor rcx, rcx
.verify:
    mov rax, 0x1122334455667788
    add rax, rcx
    cmp [rbx+rcx], rax
    jne bad
    add rcx, 8
    cmp rcx, 16384
    jb .verify
    inc qword [calls]
    ; shrink to two pages: retained data must survive
    mov rdi, USER_HEAP_BASE+8192
    gate USER_BRK
    mov rcx, USER_HEAP_BASE+8192
    cmp rax, rcx
    jne bad
    xor rcx, rcx
.kept:
    mov rax, 0x1122334455667788
    add rax, rcx
    cmp [rbx+rcx], rax
    jne bad
    add rcx, 8
    cmp rcx, 8192
    jb .kept
    ; grow by a partial page: newly mapped page is zeroed
    mov rdi, USER_HEAP_BASE+8192+300
    gate USER_BRK
    mov rcx, USER_HEAP_BASE+8192+300
    cmp rax, rcx
    jne bad
    xor rcx, rcx
.zero:
    cmp qword [rbx+8192+rcx], 0
    jne bad
    add rcx, 8
    cmp rcx, 4096
    jb .zero
    xor rcx, rcx
.kept2:
    mov rax, 0x1122334455667788
    add rax, rcx
    cmp [rbx+rcx], rax
    jne bad
    add rcx, 8
    cmp rcx, 8192
    jb .kept2
    ; first anonymous mapping begins at the reserved region base, zeroed
    mov edi, 4096
    xor esi, esi
    gate USER_MMAP
    mov rcx, USER_MMAP_BASE
    cmp rax, rcx
    jb bad
    test rax, 4095
    jnz bad
    mov r12, rax
    xor rcx, rcx
.mzero:
    cmp qword [r12+rcx], 0
    jne bad
    add rcx, 8
    cmp rcx, 4096
    jb .mzero
    mov rax, 0x0f0e0d0c0b0a0908
    mov [r12], rax
    cmp [r12], rax
    jne bad
    mov rdi, r12
    mov esi, 4096
    gate USER_MUNMAP
    expect 0
    ; next mapping continues upward, independently zeroed and writable
    mov edi, 8192
    xor esi, esi
    gate USER_MMAP
    test rax, rax
    js bad
    cmp rax, r12
    jbe bad
    mov r13, rax
    xor rcx, rcx
.mzero2:
    cmp qword [r13+rcx], 0
    jne bad
    add rcx, 8
    cmp rcx, 8192
    jb .mzero2
    mov rdx, 0x7e7e7e7e7e7e7e7e
    mov [r13+8192-8], rdx
    mov rdi, r13
    mov esi, 8192
    gate USER_MUNMAP
    expect 0
    ; identity is stable
    gate USER_GETPID
    test rax, rax
    jz bad
    mov [pid0], rax
    gate USER_GETPID
    cmp rax, [pid0]
    jne bad
    ; monotonic clock and blocking sleep
    mov edi, USER_CLOCK_TICKS
    gate USER_CLOCK
    mov [clock0], rax
    mov edi, 20
    gate USER_SLEEP
    expect 0
    mov edi, USER_CLOCK_TICKS
    gate USER_CLOCK
    mov rcx, [clock0]
    add rcx, 2
    cmp rax, rcx
    jb bad
    mov edi, USER_CLOCK_MS
    gate USER_CLOCK
    cmp rax, 20
    jb bad
    ; final partial-page break semantics keep the first bytes mapped
    mov qword [rbx], 0x1234
    mov rdi, USER_HEAP_BASE+300
    gate USER_BRK
    mov rcx, USER_HEAP_BASE+300
    cmp rax, rcx
    jne bad
    cmp qword [rbx], 0x1234
    jne bad
    mov rdi, USER_HEAP_BASE
    gate USER_BRK
    cmp rax, rbx
    jne bad
    jmp done
; ---------------------------------------------------------------------------
; Test-only bump allocator on top of brk. r12 = bump, r13 = committed break.
; In: rdi = size. Out: rax = block address. Never part of future libc.
alloc_block:
    mov rax, r12
    add r12, rdi
    add r12, 15
    and r12, -16
    cmp r12, r13
    jbe .alloc_done
    mov r10, rax          ; brk clobbers RAX; keep the block address
    mov rbp, r12
    add rbp, 4095
    and rbp, -4096
    mov rdi, rbp
    gate USER_BRK
    cmp rax, rbp
    jne .alloc_fail
    mov r13, rbp
    mov rax, r10
.alloc_done:
    ret
.alloc_fail:
    jmp bad
bump_mode:
    mov rbx, USER_HEAP_BASE
    mov rdi, USER_HEAP_BASE
    gate USER_BRK
    cmp rax, rbx
    jne bad
    mov r12, rbx
    mov r13, rbx
    lea r14, [block_addr]
    lea r8, [block_size]
    lea r9, [sizes]
    xor r15, r15
.alloc_loop:
    mov rdi, [r9+r15*8]
    mov [r8+r15*8], rdi
    call alloc_block
    mov [r14+r15*8], rax
    inc r15
    cmp r15, 8
    jb .alloc_loop
    ; unique byte pattern per block
    xor r15, r15
.fill_blocks:
    mov rdi, [r14+r15*8]
    mov rcx, [r8+r15*8]
    mov r9b, 0x40
    add r9b, r15b
    xor rdx, rdx
.byte_fill:
    mov [rdi+rdx], r9b
    inc rdx
    cmp rdx, rcx
    jb .byte_fill
    inc r15
    cmp r15, 8
    jb .fill_blocks
    ; verify contents and that blocks never overlap
    mov r15, 1
.overlap:
    mov rax, [r14+r15*8]
    test rax, 15
    jnz bad
    mov rcx, [r14+r15*8-8]
    add rcx, [r8+r15*8-8]
    cmp rax, rcx
    jb bad
    inc r15
    cmp r15, 8
    jb .overlap
    xor r15, r15
.verify_blocks:
    mov rdi, [r14+r15*8]
    mov rcx, [r8+r15*8]
    mov r9b, 0x40
    add r9b, r15b
    xor rdx, rdx
.byte_check:
    cmp [rdi+rdx], r9b
    jne bad
    inc rdx
    cmp rdx, rcx
    jb .byte_check
    inc r15
    cmp r15, 8
    jb .verify_blocks
    ; committed break is page aligned and at or above the bump pointer
    test r13, 4095
    jnz bad
    cmp r12, r13
    ja bad
    ; untouched tail of the committed page is still zero
    mov rcx, r13
    sub rcx, r12
    cmp rcx, 64
    jbe .tail_size
    mov rcx, 64
.tail_size:
    xor rdx, rdx
.tail:
    cmp rdx, rcx
    jae .tail_done
    cmp byte [r12+rdx], 0
    jne bad
    inc rdx
    jmp .tail
.tail_done:
    ; shrink to one page: block zero is retained and intact
    mov rdi, USER_HEAP_BASE+4096
    gate USER_BRK
    mov rcx, USER_HEAP_BASE+4096
    cmp rax, rcx
    jne bad
    mov rdi, [r14]
    mov rcx, [r8]
    mov r9b, 0x40
    xor rdx, rdx
.zero_check:
    cmp [rdi+rdx], r9b
    jne bad
    inc rdx
    cmp rdx, rcx
    jb .zero_check
    ; regrow to four pages: retained block intact, fresh bytes zeroed
    mov rdi, USER_HEAP_BASE+16384
    gate USER_BRK
    mov rcx, USER_HEAP_BASE+16384
    cmp rax, rcx
    jne bad
    mov rdi, [r14]
    mov rcx, [r8]
    mov r9b, 0x40
    xor rdx, rdx
.zero_check2:
    cmp [rdi+rdx], r9b
    jne bad
    inc rdx
    cmp rdx, rcx
    jb .zero_check2
    ; Page 0 was never unmapped; the second page was and must read back zero
    ; after the regrow even though it held block-4 data before the shrink.
    mov rdi, USER_HEAP_BASE+4096
    mov rcx, 32
    xor rdx, rdx
.rezero:
    cmp byte [rdi+rdx], 0
    jne bad
    inc rdx
    cmp rdx, rcx
    jb .rezero
    mov rdi, USER_HEAP_BASE
    gate USER_BRK
    cmp rax, rbx
    jne bad
    jmp done
; ---------------------------------------------------------------------------
bad_brk:
    mov rbx, USER_HEAP_BASE
    xor edi, edi
    gate USER_BRK
    cmp rax, rbx
    jne bad
    mov rdi, USER_HEAP_BASE-1
    gate USER_BRK
    expect -USER_EINVAL
    mov rdi, USER_HEAP_LIMIT+1
    gate USER_BRK
    expect -USER_EINVAL
    mov rdi, USER_STACK_BASE
    gate USER_BRK
    expect -USER_EINVAL
    mov rdi, 0xffffff8000000000
    gate USER_BRK
    expect -USER_EINVAL
    mov rdi, 0x0000800000000000
    gate USER_BRK
    expect -USER_EINVAL
    mov rdi, 0xffffffffffffffff
    gate USER_BRK
    expect -USER_EINVAL
    ; every rejected request preserved the old break
    xor edi, edi
    gate USER_BRK
    cmp rax, rbx
    jne bad
    ; and valid growth still works afterwards
    mov rdi, USER_HEAP_BASE+4096
    gate USER_BRK
    mov rcx, USER_HEAP_BASE+4096
    cmp rax, rcx
    jne bad
    mov rdx, 0x55aa55aa55aa55aa
    mov [rbx], rdx
    cmp [rbx], rdx
    jne bad
    mov rdi, USER_HEAP_BASE
    gate USER_BRK
    cmp rax, rbx
    jne bad
    jmp done
; ---------------------------------------------------------------------------
bad_map:
    mov r12, USER_MMAP_BASE
    xor edi, edi
    xor esi, esi
    gate USER_MMAP
    expect -USER_EINVAL
    mov edi, 4096
    mov esi, 1
    gate USER_MMAP
    expect -USER_EINVAL
    mov edi, (USER_MMAP_MAX_PAGES+1)*4096
    xor esi, esi
    gate USER_MMAP
    expect -USER_E2BIG
    mov rdi, r12
    mov esi, 4096
    gate USER_MUNMAP
    expect -USER_EINVAL
    lea rdi, [r12+1]
    mov esi, 4096
    gate USER_MUNMAP
    expect -USER_EINVAL
    mov rdi, r12
    xor esi, esi
    gate USER_MUNMAP
    expect -USER_EINVAL
    mov rdi, USER_CODE
    mov esi, 4096
    gate USER_MUNMAP
    expect -USER_EINVAL
    mov rdi, 0xffffff8000000000
    mov esi, 4096
    gate USER_MUNMAP
    expect -USER_EINVAL
    ; a heap page is not an anonymous mapping record
    mov rdi, USER_HEAP_BASE
    gate USER_BRK
    mov rdi, USER_HEAP_BASE
    mov esi, 4096
    gate USER_MUNMAP
    expect -USER_EINVAL
    mov rdi, USER_HEAP_BASE
    gate USER_BRK
    ; valid mapping, then double unmap rejection
    mov edi, 4096
    xor esi, esi
    gate USER_MMAP
    cmp rax, r12
    jb bad
    mov r13, rax
    mov rdi, r13
    mov esi, 4096
    gate USER_MUNMAP
    expect 0
    mov rdi, r13
    mov esi, 4096
    gate USER_MUNMAP
    expect -USER_EINVAL
    ; exact length is required
    mov edi, 8192
    xor esi, esi
    gate USER_MMAP
    test rax, rax
    js bad
    mov r13, rax
    mov rdi, r13
    mov esi, 4096
    gate USER_MUNMAP
    expect -USER_EINVAL
    mov rdi, r13
    mov esi, 8192
    gate USER_MUNMAP
    expect 0
    ; bounded record table: sixteen live mappings, then refusal
    lea r14, [map_table]
    xor r15, r15
.map_fill:
    mov edi, 4096
    xor esi, esi
    gate USER_MMAP
    test rax, rax
    js bad
    test r15, r15
    jnz .map_skip
    mov [r14], rax
.map_skip:
    inc r15
    cmp r15, USER_MMAP_MAX
    jb .map_fill
    mov edi, 4096
    xor esi, esi
    gate USER_MMAP
    expect -USER_ENOMEM
    mov rdi, [r14]
    mov esi, 4096
    gate USER_MUNMAP
    expect 0
    mov edi, 4096
    xor esi, esi
    gate USER_MMAP
    test rax, rax
    js bad
    jmp done
; ---------------------------------------------------------------------------
exec_heap:
    mov rdi, USER_HEAP_BASE+4096
    gate USER_BRK
    mov rcx, USER_HEAP_BASE+4096
    cmp rax, rcx
    jne bad
    mov rdi, USER_HEAP_BASE
    mov byte [rdi], 0xc3
    jmp rdi
    ud2
; ---------------------------------------------------------------------------
inject_mode:
    mov rbx, USER_HEAP_BASE
    xor edi, edi
    gate USER_BRK
    cmp rax, rbx
    jne bad
    mov rdi, USER_HEAP_BASE+8192
    gate USER_BRK
    mov rcx, USER_HEAP_BASE+8192
    cmp rax, rcx
    jne bad
    mov rdx, 0xdeadbeefcafef00d
    mov [rbx], rdx
    mov rdx, 0x0123456789abcdef
    mov [rbx+8192-8], rdx
    mov qword [phase], 1
.wait_inject:
    cmp qword [release], 1
    jb .wait_inject
    mov rdi, USER_HEAP_BASE+8192+16384
    gate USER_BRK
    expect -USER_ENOMEM
    xor edi, edi
    gate USER_BRK
    mov rcx, USER_HEAP_BASE+8192
    cmp rax, rcx
    jne bad
    mov rdx, 0xdeadbeefcafef00d
    cmp [rbx], rdx
    jne bad
    mov rdx, 0x0123456789abcdef
    cmp [rbx+8192-8], rdx
    jne bad
    mov qword [phase], 2
.wait_clean:
    cmp qword [release], 2
    jb .wait_clean
    mov rdi, USER_HEAP_BASE+8192+16384
    gate USER_BRK
    mov rcx, USER_HEAP_BASE+8192+16384
    cmp rax, rcx
    jne bad
    xor rcx, rcx
.izero:
    cmp qword [rbx+8192+rcx], 0
    jne bad
    add rcx, 8
    cmp rcx, 16384
    jb .izero
    mov rdx, 0xdeadbeefcafef00d
    cmp [rbx], rdx
    jne bad
    jmp done
; ---------------------------------------------------------------------------
; Peer heaps at identical virtual addresses. Pattern encodes the PID, so any
; cross-process leakage or preemption corruption fails immediately.
peer_mode:
    mov rbx, USER_HEAP_BASE
    mov rdi, USER_HEAP_BASE
    gate USER_BRK
    cmp rax, rbx
    jne bad
    mov rdi, USER_HEAP_BASE+16384
    gate USER_BRK
    mov rcx, USER_HEAP_BASE+16384
    cmp rax, rcx
    jne bad
    gate USER_GETPID
    mov [pid0], rax
    mov r12, rax
    xor rcx, rcx
.pfill:
    mov rax, r12
    shl rax, 32
    mov rdx, rcx
    or rax, rdx
    mov [rbx+rcx], rax
    add rcx, 8
    cmp rcx, 16384
    jb .pfill
    mov qword [phase], 1
    mov qword [counter], 0
.round:
    xor rcx, rcx
.pverify:
    mov rax, r12
    shl rax, 32
    mov rdx, rcx
    or rax, rdx
    cmp [rbx+rcx], rax
    jne bad
    add rcx, 8
    cmp rcx, 16384
    jb .pverify
    gate USER_GETPID
    cmp rax, r12
    jne bad
    mov edi, USER_CLOCK_TICKS
    gate USER_CLOCK
    cmp rax, [clock0]
    jb bad
    mov [clock0], rax
    mov rcx, 2000000
.pdelay:
    dec rcx
    jnz .pdelay
    mov edi, 10
    gate USER_SLEEP
    expect 0
    inc qword [counter]
    cmp qword [counter], 8
    jb .round
    mov qword [phase], 2
.pwait1:
    cmp qword [release], 1
    jb .pwait1
    mov rdi, USER_HEAP_BASE+8192
    gate USER_BRK
    mov rdi, USER_HEAP_BASE+8192
    cmp rax, rdi
    jne bad
    xor rcx, rcx
.pverify2:
    mov rax, r12
    shl rax, 32
    mov rdx, rcx
    or rax, rdx
    cmp [rbx+rcx], rax
    jne bad
    add rcx, 8
    cmp rcx, 8192
    jb .pverify2
    mov qword [phase], 3
.pwait2:
    cmp qword [release], 2
    jb .pwait2
    xor rcx, rcx
.pverify3:
    mov rax, r12
    shl rax, 32
    mov rdx, rcx
    or rax, rdx
    cmp [rbx+rcx], rax
    jne bad
    add rcx, 8
    cmp rcx, 8192
    jb .pverify3
    jmp done
; ---------------------------------------------------------------------------
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
sizes: dq 17, 100, 1000, 33, 4096, 5, 777, 250
section .data
mode: dq 0
phase: dq 0
release: dq 0
failure_rax: dq 0
calls: dq 0
counter: dq 0
pid0: dq 0
clock0: dq 0
section .bss align=16
map_table: resq 1
block_addr: resq 8
block_size: resq 8
