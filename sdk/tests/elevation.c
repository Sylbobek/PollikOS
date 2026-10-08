#include <pollikos/process.h>
#include <pollikos/session.h>
#include <pollikos/fs.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int errors;
#define CHECK(x) do{if(!(x)){printf("ELEVATION_FAIL line=%d\n",__LINE__);errors++;}}while(0)
static void wait_child(long pid){pollikos_wait_t status;CHECK(pid>0);if(pid>0)CHECK(pollikos_waitpid(pid,&status)==pid&&status.kind==POLLIKOS_WAIT_EXITED&&status.code==0);}
int main(int argc,char **argv){
    long rights=pollikos_session_control(USER_SESSION_RIGHTS);
    if(argc>1&&!strcmp(argv[1],"limited")){
        CHECK(rights==USER_CAP_FILE_READ);CHECK(pollikos_session_control(USER_SESSION_ELEVATE)<0);return errors;
    }
    if(argc>1){
        CHECK(rights==USER_CAP_ADMIN_ALL);
        int fd=open("/etc/admin-probe",O_WRONLY|O_CREAT|O_TRUNC);CHECK(fd>=0);if(fd>=0)CHECK(close(fd)==0);
        CHECK(unlink("/etc/admin-probe")==0);
        if(!strcmp(argv[1],"admin")){const char *child[]={"elevation","nested",NULL};wait_child(pollikos_spawn("/bin/elevation.pol",child,NULL));}
        return errors;
    }
    CHECK(rights==USER_CAP_DEFAULT);
    const char *admin[]={"elevation","admin",NULL},*limited[]={"elevation","limited",NULL};
    CHECK(pollikos_spawn_rights("/bin/elevation.pol",admin,NULL,USER_CAP_ADMIN_ALL)<0);
    CHECK(open("/etc/admin-probe",O_WRONLY|O_CREAT)<0);
    wait_child(pollikos_spawn_rights("/bin/elevation.pol",limited,NULL,USER_CAP_FILE_READ));
    CHECK(pollikos_session_control(USER_SESSION_ELEVATE)<0); /* Harness cancels. */
    CHECK(pollikos_spawn_rights("/bin/elevation.pol",admin,NULL,USER_CAP_ADMIN_ALL)<0);
    CHECK(pollikos_session_control(USER_SESSION_ELEVATE)==0); /* Wrong, then correct password. */
    CHECK(pollikos_session_control(USER_SESSION_RIGHTS)==rights);
    wait_child(pollikos_spawn_rights("/bin/elevation.pol",admin,NULL,USER_CAP_ADMIN_ALL));
    CHECK(pollikos_spawn_rights("/bin/elevation.pol",admin,NULL,USER_CAP_ADMIN_ALL)<0);
    if(!errors)puts("ELEVATION_PASS: cancellation/no escalation, native administrator rights/write/inheritance, one-use grant");
    return errors?1:0;
}
