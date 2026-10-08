#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pollikos/fs.h>
#include <pollikos/process.h>
#include <pollikos/session.h>
#include <pollikos/devices.h>
#include <pollikos/window.h>
#include <sys/stat.h>
#include <unistd.h>
static int errors;
#define CHECK(x) do { if(!(x)) { printf("SECURITY_GUEST_FAIL line=%d errno=%d\n",__LINE__,errno); ++errors; } } while(0)
int main(int argc,char **argv) {
    long session=pollikos_session_control(USER_SESSION_ID);
    CHECK(session>0);
    if(argc>1) {
        CHECK(session==strtol(argv[1],NULL,10));
        if(argc>2) {
            CHECK(open("/home/sandbox-write",O_WRONLY|O_CREAT)<0 && errno==EACCES);
            CHECK(pollikos_device(USER_DEVICE_VOLUME_SET,50)==-USER_EPERM);
            CHECK(pollikos_device(USER_DEVICE_POWER,0)==-USER_EPERM);
            CHECK(pollikos_device(USER_DEVICE_POWER,1)==-USER_EPERM);
            CHECK(pollikos_session_control(USER_SESSION_LOCK)<0 && errno==USER_EPERM);
            CHECK(__pollikos_syscall3(USER_STREAM_OPEN,0,80,0)==-USER_EPERM);
            CHECK(pollikos_window_create(320,200,"denied")==-USER_EPERM);
        }
        return errors;
    }
    CHECK(open("/etc/account.db",O_RDONLY)<0 && errno==EACCES);
    CHECK(open("/home/../etc/account.db",O_WRONLY|O_TRUNC)<0 && errno==EACCES);
    CHECK(unlink("/etc/account.db")<0 && errno==EACCES);
    CHECK(rename("/etc/account.db","/home/stolen")<0 && errno==EACCES);
    struct stat st; CHECK(stat("/etc/account.db",&st)<0 && errno==EACCES);
    CHECK(open("/bin/security-overwrite",O_WRONLY|O_CREAT)<0 && errno==EACCES);
    int append=open("/home/append-probe",O_WRONLY|O_CREAT|O_APPEND);CHECK(append>=0);
    if(append>=0) CHECK(close(append)==0);
    FILE *stream=fopen("/home/append-stdio","ab");CHECK(stream!=NULL);
    if(stream) fclose(stream);
    int fd=open("/home/security-range",O_RDWR|O_CREAT|O_TRUNC); CHECK(fd>=0);
    if(fd>=0) {
        CHECK(write(fd,"safe",4)==4); CHECK(lseek(fd,0x7fffffff,SEEK_SET)==0x7fffffff);
        CHECK(write(fd,"x",1)<0 && errno==ENOSPC);
        CHECK(fstat(fd,&st)==0 && st.st_size==4);
        CHECK(lseek(fd,0,SEEK_SET)==0); char data[4];
        CHECK(read(fd,data,4)==4 && memcmp(data,"safe",4)==0); CHECK(close(fd)==0);
    }
    char value[24]; snprintf(value,sizeof(value),"%ld",session);
    const char *args[]={"security_session",value,NULL};
    long child=pollikos_spawn("/bin/security_session.pol",args,NULL); CHECK(child>0);
    if(child>0) { pollikos_wait_t status; CHECK(pollikos_waitpid(child,&status)==child && status.kind==POLLIKOS_WAIT_EXITED && status.code==0); }
    const char *limited[]={"security_session",value,"sandbox",NULL};
    child=pollikos_spawn_rights("/bin/security_session.pol",limited,NULL,USER_CAP_FILE_READ);
    CHECK(child>0);
    if(child>0) { pollikos_wait_t status;CHECK(pollikos_waitpid(child,&status)==child && status.code==0); }
    if(!errors) printf("SECURITY_GUEST_PASS session=%ld\n",session);
    return errors?1:0;
}
