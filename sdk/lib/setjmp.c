#include <setjmp.h>
/* SysV AMD64 callee-saved integer state: rbx, rbp, r12-r15, post-return rsp,
 * and the return PC. FPU/SIMD state is outside the PollikOS process ABI. */
__attribute__((naked, returns_twice)) int setjmp(jmp_buf environment) {
    __asm__ volatile(
        "mov %rbx,0(%rdi)\n"
        "mov %rbp,8(%rdi)\n"
        "mov %r12,16(%rdi)\n"
        "mov %r13,24(%rdi)\n"
        "mov %r14,32(%rdi)\n"
        "mov %r15,40(%rdi)\n"
        "lea 8(%rsp),%rax\n"
        "mov %rax,48(%rdi)\n"
        "mov (%rsp),%rax\n"
        "mov %rax,56(%rdi)\n"
        "xor %eax,%eax\n"
        "ret");
}
__attribute__((naked, noreturn)) void longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "mov %esi,%eax\n"
        "test %eax,%eax\n"
        "jnz 1f\n"
        "inc %eax\n"
        "1:\n"
        "mov 0(%rdi),%rbx\n"
        "mov 8(%rdi),%rbp\n"
        "mov 16(%rdi),%r12\n"
        "mov 24(%rdi),%r13\n"
        "mov 32(%rdi),%r14\n"
        "mov 40(%rdi),%r15\n"
        "mov 48(%rdi),%rsp\n"
        "jmp *56(%rdi)");
}
