#include <pollikos/session.h>
#include <pollikos/fs.h>
#include <pollikos/syscall.h>
#include <pollikos/process.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"launch")) {
        const char *args[]={"settings",NULL};
        if(pollikos_spawn("/bin/settings.pol",args,NULL)<0)return 1;
        puts("RESET_SETTINGS_LAUNCHED");return 0;
    }
    if(pollikos_session_control(USER_SESSION_FACTORY_RESET)>=0){puts("RESET_RIGHTS_FAIL ordinary process accepted");return 1;}
    int fd=open("/etc/factory-reset.pending",O_RDONLY);
    if(fd>=0){close(fd);puts("RESET_RIGHTS_FAIL request marker unexpectedly present");return 1;}
    puts("RESET_RIGHTS_PASS ordinary process cannot initiate reset or write marker");return 0;
}
