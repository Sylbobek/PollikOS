#include <pollikos/system.h>
#include <pollikos/process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
    PollikSystemInfo info;PollikProcessInfo p;int errors=0;
    #define CHECK(x) do{if(!(x)){printf("SYSTEM_QUERY_FAIL line=%d\n",__LINE__);errors++;}}while(0)
    if(argc==3 && !strcmp(argv[1],"limited-signal")) {
        long pid=strtol(argv[2],NULL,10);
        CHECK(__pollikos_syscall2(USER_SIGNAL_SEND,(uint64_t)pid,0)==-USER_EPERM);
        CHECK(__pollikos_syscall2(USER_SIGNAL_SEND,(uint64_t)pid,15)==-USER_EPERM);
        return errors?1:0;
    }
    CHECK(pollikos_system_info(&info)==0);CHECK(info.version==1 && info.size==sizeof(info));
    CHECK(info.ram_bytes>0 && info.ram_free_bytes<=info.ram_bytes && info.fs_free_blocks<=info.fs_blocks);
    CHECK(info.fs_block_size==1024 && info.session && info.username[0]);
    CHECK(__pollikos_syscall4(USER_SYSTEM_QUERY,POLLIK_QUERY_SYSTEM,0,0,sizeof(info))==-USER_EFAULT);
    CHECK(__pollikos_syscall4(USER_SYSTEM_QUERY,POLLIK_QUERY_SYSTEM,0,(uint64_t)(uintptr_t)&info,sizeof(info)-1)==-USER_EINVAL);
    CHECK(__pollikos_syscall4(USER_SYSTEM_QUERY,999,0,(uint64_t)(uintptr_t)&info,sizeof(info))==-USER_EINVAL);
    CHECK(pollikos_process_info(PROCESS_MAX+1,&p)==-USER_EINVAL);
    unsigned index=0,own=0;long result;
    while((result=pollikos_process_info(index,&p))>0){CHECK(p.next_index>index && p.state<7);if(p.pid==(uint64_t)pollikos_getpid())own++;
        if(!(info.rights&USER_CAP_ADMIN))CHECK(p.session==info.session);index=p.next_index;}
    CHECK(result==0 && own==1);
    if(info.rights&USER_CAP_ADMIN) {
        char pid[24];snprintf(pid,sizeof(pid),"%ld",pollikos_getpid());
        const char *args[]={"system_query","limited-signal",pid,NULL};
        long child=pollikos_spawn_rights("/bin/system_query.pol",args,NULL,USER_CAP_DEFAULT);pollikos_wait_t status;
        CHECK(child>0 && pollikos_waitpid(child,&status)==child && status.kind==POLLIKOS_WAIT_EXITED && status.code==0);
    }
    if(!errors)puts("SYSTEM_QUERY_PASS real accounting, process iteration/session filtering, invalid pointer/size/op/cursor");return errors?1:0;
}
