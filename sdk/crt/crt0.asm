; PollikOS x86_64 userspace startup object.
; PollikOS startup ABI v1 delivers argc in RDI, argv in RSI, envp in RDX and
; the ABI version in RCX. These are already the first four System V argument
; registers, so _start only forwards them and never re-derives the stack.
; All ordinary runtime logic lives in libc (__libc_start).
bits 64
default rel
global _start
extern __libc_start
section .text
_start:
    call __libc_start
    ud2

; POSIX handler return path. The kernel restores the interrupted GPR and FPU
; image from its process record after validating the token at the user RSP.
global __pollikos_sigreturn
__pollikos_sigreturn:
    mov rdi, rsp
    mov eax, 0x504f0030
    syscall
    ud2
