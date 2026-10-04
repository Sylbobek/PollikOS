#ifndef POLLIKOS_SYSCALL_H
#define POLLIKOS_SYSCALL_H
#include <stdint.h>
#include <pollikos_abi.h>
/* Generated from kernel/arch/x86_64/user_abi.h at build time, so operation
 * numbers and error codes always match the running kernel. */
#include <pollikos/abi_numbers.h>
/* PollikOS SYSCALL ABI: RAX number/result, args RDI/RSI/RDX/R10/R8/R9.
 * Only RCX and R11 are clobbered. Current operations use at most three
 * arguments; there is deliberately no inline assembly outside this header. */
static __inline__ int64_t __pollikos_syscall0(uint64_t number) {
    int64_t result;
    __asm__ volatile("syscall" : "=a"(result) : "a"(number) : "rcx", "r11", "memory");
    return result;
}
static __inline__ int64_t __pollikos_syscall1(uint64_t number, uint64_t first) {
    int64_t result;
    __asm__ volatile("syscall" : "=a"(result)
        : "a"(number), "D"(first) : "rcx", "r11", "memory");
    return result;
}
static __inline__ int64_t __pollikos_syscall2(uint64_t number, uint64_t first, uint64_t second) {
    int64_t result;
    __asm__ volatile("syscall" : "=a"(result)
        : "a"(number), "D"(first), "S"(second) : "rcx", "r11", "memory");
    return result;
}
static __inline__ int64_t __pollikos_syscall3(uint64_t number, uint64_t first,
                                             uint64_t second, uint64_t third) {
    int64_t result;
    __asm__ volatile("syscall" : "=a"(result)
        : "a"(number), "D"(first), "S"(second), "d"(third) : "rcx", "r11", "memory");
    return result;
}
static __inline__ int64_t __pollikos_syscall4(uint64_t number, uint64_t first, uint64_t second,
                                             uint64_t third, uint64_t fourth) {
    register uint64_t r10 __asm__("r10") = fourth;
    int64_t result;
    __asm__ volatile("syscall" : "=a"(result)
        : "a"(number), "D"(first), "S"(second), "d"(third), "r"(r10)
        : "rcx", "r11", "memory");
    return result;
}
/* Returns the number of ABI-info bytes written, or a negative PollikOS error. */
static __inline__ int64_t pollikos_get_abi_info(PollikAbiInfo *info) {
    return __pollikos_syscall1(USER_ABI_INFO, (uint64_t)info);
}
/* POSIX-style wrapper helper: negative PollikOS results become -1 + errno. */
long __pollikos_fail(int64_t result);
#endif
