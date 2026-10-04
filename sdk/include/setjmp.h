#ifndef POLLIKOS_SETJMP_H
#define POLLIKOS_SETJMP_H
typedef unsigned long jmp_buf[8];
int setjmp(jmp_buf environment) __attribute__((returns_twice));
void longjmp(jmp_buf environment, int value) __attribute__((noreturn));
#endif
