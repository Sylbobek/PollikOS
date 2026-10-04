#ifndef POLLIKOS_STDDEF_H
#define POLLIKOS_STDDEF_H
/* Freestanding LP64 definitions for PollikOS x86_64 userspace. */
typedef unsigned long size_t;
typedef long ptrdiff_t;
#define NULL ((void *)0)
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif
