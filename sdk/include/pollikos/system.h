#ifndef POLLIKOS_SYSTEM_H
#define POLLIKOS_SYSTEM_H
#include <pollikos/syscall.h>
#include <pollikos_system.h>
/* Read-only kernel snapshots. Process iteration returns 1 / end 0 / error <0;
 * normal callers see their session, administrators see all processes. */
static inline long pollikos_system_info(PollikSystemInfo *info) {
    return __pollikos_syscall4(USER_SYSTEM_QUERY,POLLIK_QUERY_SYSTEM,0,(uint64_t)(uintptr_t)info,sizeof(*info));
}
static inline long pollikos_process_info(unsigned index,PollikProcessInfo *info) {
    return __pollikos_syscall4(USER_SYSTEM_QUERY,POLLIK_QUERY_PROCESS,index,(uint64_t)(uintptr_t)info,sizeof(*info));
}
#endif
