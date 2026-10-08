#include <pollikos/session.h>
#include <pollikos/syscall.h>
#include <errno.h>
long pollikos_session_control(unsigned operation) {
    long result=__pollikos_syscall1(USER_SESSION,operation);
    if(result<0) { errno=(int)-result; return -1; } return result;
}
long pollikos_session_elevate(const char *path){
    long result=__pollikos_syscall2(USER_SESSION,USER_SESSION_ELEVATE_APP,(uint64_t)(uintptr_t)path);
    if(result<0){errno=(int)-result;return -1;}return result;
}
