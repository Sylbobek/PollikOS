#include <pollikos/process.h>
#include <sys/wait.h>
#include <stdio.h>
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    long previous=0;
    for(int i=0;i<5;++i){
        const char *child_argv[]={argv[1],0};
        long pid=pollikos_spawn(argv[1],child_argv,0);
        if(pid<=0 || pid==previous)return 3;
        printf("[LIFE] start cycle=%d pid=%ld\n",i,pid);
        int status=-1;
        if(waitpid((pid_t)pid,&status,0)!=(pid_t)pid || !WIFEXITED(status) || WEXITSTATUS(status)!=0)return 4;
        printf("[LIFE] reaped cycle=%d pid=%ld exit=0\n",i,pid);
        previous=pid;
    }
    puts("PASS window lifecycle: five fresh PIDs, close exits, wait reaps");
    return 0;
}
