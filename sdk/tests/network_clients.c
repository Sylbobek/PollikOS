#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pollikos/net.h>
#include <pollikos/process.h>
#include <pollikos/time.h>
int main(int argc,char **argv) {
    if(argc==3 && !strcmp(argv[1],"peer"))
        return __pollikos_syscall1(USER_STREAM_STATUS,strtoull(argv[2],NULL,10))==-USER_EBADF?0:1;
    if(argc!=2) return 2;
    unsigned port=(unsigned)strtoul(argv[1],NULL,10);
    long raw=__pollikos_syscall3(USER_STREAM_OPEN,(uint64_t)(uintptr_t)"10.0.2.2",port,0);
    if(raw<0) return 3;
    char value[32];snprintf(value,sizeof(value),"%ld",raw);
    const char *args[]={"network_clients","peer",value,NULL};
    long child=pollikos_spawn("/bin/network_clients.pol",args,NULL);pollikos_wait_t status;
    if(child<0 || pollikos_waitpid(child,&status)!=child || status.code) return 4;
    if(__pollikos_syscall1(USER_STREAM_CLOSE,raw)<0 || __pollikos_syscall1(USER_STREAM_STATUS,raw)!=-USER_EBADF) return 5;
    char url[128],body[2][32]={{0}};unsigned used[2]={0};int done[2]={0};long handles[2];
    for(unsigned i=0;i<2;i++) {
        snprintf(url,sizeof(url),"http://10.0.2.2:%u/%s",port,i?"chunked":"first");
        handles[i]=pollikos_http_open(url);if(handles[i]<0) return 6;
    }
    long limit=pollikos_monotonic_ms()+30000;
    while((!done[0] || !done[1]) && pollikos_monotonic_ms()<limit) {
        for(unsigned i=0;i<2;i++) if(!done[i]) {
            long n=pollikos_http_read(handles[i],body[i]+used[i],sizeof(body[i])-used[i]-1);
            if(n==-USER_EAGAIN) continue;
            if(n<0) { printf("NETWORK_FAIL read=%ld\n",n);return 7; }
            if(!n) done[i]=1;else used[i]+=(unsigned)n;
        }
        pollikos_sleep_ms(1);
    }
    if(!done[0] || !done[1] || strcmp(body[0],"alpha") || strcmp(body[1],"beta")) return 8;
    for(unsigned i=0;i<2;i++) {
        if(pollikos_http_close(handles[i])<0 || pollikos_http_close(handles[i])!=-USER_EBADF) return 9;
    }
    puts("NETWORK_CLIENTS_PASS simultaneous requests, chunked body, peer ownership and stale handles");return 0;
}
