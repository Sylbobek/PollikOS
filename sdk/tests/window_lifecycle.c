#include <pollikos/process.h>
#include <sys/wait.h>
#include <stdio.h>
#include <unistd.h>
static int record(const char *action,int cycle,long pid) {
    char text[96];
    int n=snprintf(text,sizeof(text),"[LIFE] %s cycle=%d pid=%ld%s\n",action,cycle,pid,
                   action[0]=='r'?" exit=0":"");
    return n>0 && n<(int)sizeof(text) && write(1,text,(size_t)n)==n;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    long previous=0;
    for(int i=0;i<5;++i){
        const char *child_argv[]={argv[1],0};
        long pid=pollikos_spawn(argv[1],child_argv,0);
        if(pid<=0 || pid==previous)return 3;
        if(!record("start",i,pid))return 5;
        int status=-1;
        if(waitpid((pid_t)pid,&status,0)!=(pid_t)pid || !WIFEXITED(status) || WEXITSTATUS(status)!=0)return 4;
        if(!record("reaped",i,pid))return 5;
        previous=pid;
    }
    puts("PASS window lifecycle: five fresh PIDs, close exits, wait reaps");
    return 0;
}
