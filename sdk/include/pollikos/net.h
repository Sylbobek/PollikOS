#ifndef POLLIKOS_NET_H
#define POLLIKOS_NET_H
#include <stdint.h>
#include <pollikos/syscall.h>

/* Nonblocking HTTP/1.1 over the kernel's active IPv4 interface. read() returns
 * bytes, 0 at EOF, or a negative PollikOS error such as -USER_EAGAIN. */
static __inline__ long pollikos_http_open(const char *url) {
    return (long)__pollikos_syscall1(USER_HTTP_OPEN,(uint64_t)(uintptr_t)url);
}
static __inline__ long pollikos_http_read(long handle, void *buffer, unsigned long capacity) {
    return (long)__pollikos_syscall3(USER_HTTP_READ,(uint64_t)handle,
        (uint64_t)(uintptr_t)buffer,(uint64_t)capacity);
}
static __inline__ long pollikos_http_close(long handle) {
    return (long)__pollikos_syscall1(USER_HTTP_CLOSE,(uint64_t)handle);
}
#endif
