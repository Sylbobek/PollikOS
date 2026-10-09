/* Command implementations shared by the interactive and -c shell paths. */
#include <pollikos/system.h>
#include <pollikos/devices.h>
#include <pollikos/net.h>
#include <time.h>
#include <fcntl.h>
static int run_command(int argc,const char *const *argv);
static int wait_code(const pollikos_wait_t *status);
static int command_error(const char *name,long result) {
    if(result<0)__pollikos_fail(result);
    perror(name);return 1;
}
static int number(const char *text,unsigned long maximum,unsigned long *result) {
    if(!text || *text<'0' || *text>'9')return 0;
    char *end;errno=0;unsigned long value=strtoul(text,&end,10);
    if(errno || *end || value>maximum)return 0;*result=value;return 1;
}
static int snapshot(PollikSystemInfo *info) {
    long result=pollikos_system_info(info);
    if(result<0)return command_error("system query",result);
    return 0;
}
static int builtin_system(int argc,const char *const *argv) {
    if(argc!=1){line_out("Usage: sysinfo | uname | uptime | free | df | whoami | id | ps | date | abi");return 2;}
    const char *name=argv[0];
    if(!strcmp(name,"uname") || !strcmp(name,"version")){line_out("PollikOS v0.0.001 x86_64 (BIOS, Ring 3 applications)");return 0;}
    if(!strcmp(name,"date")) {
        time_t now=time(NULL);if(now<0)return command_error("date",0);struct tm *t=localtime(&now);
        if(!t)return command_error("date",0);
        printf("%04d-%02d-%02d %02d:%02d:%02d UTC\n",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec);return 0;
    }
    if(!strcmp(name,"abi")) {
        PollikAbiInfo info={.size=sizeof(info)};long result=pollikos_get_abi_info(&info);
        if(result<0)return command_error(name,result);
        printf("ABI %u.%u architecture=%u transport=%u features=0x%x\n",info.major,info.minor,info.architecture,info.transport,info.features);return 0;
    }
    if(!strcmp(name,"ps")) {
        line_out("PID   PPID  STATE      UID RIGHTS HEAP-KiB NAME");unsigned index=0;PollikProcessInfo p;long result;
        static const char *states[]={"building","ready","running","blocked","exited","faulted","killed"};
        while((result=pollikos_process_info(index,&p))>0) {
            if(p.next_index<=index){line_out("Invalid process snapshot cursor");return 1;}
            printf("%-5lu %-5lu %-10s %u   0x%02x  %-8lu %s\n",(unsigned long)p.pid,(unsigned long)p.parent_pid,p.state<7?states[p.state]:"unknown",p.uid,p.rights,(unsigned long)(p.heap_bytes/1024),p.name);
            index=p.next_index;
        }
        return result<0?command_error(name,result):0;
    }
    PollikSystemInfo info;if(snapshot(&info))return 1;
    if(!strcmp(name,"whoami")){printf("%s%s\n",info.username,info.rights&USER_CAP_ADMIN?" (administrator)":"");return 0;}
    if(!strcmp(name,"id")){printf("uid=%u user=%s pid=%ld session=%lu rights=0x%x role=%s\n",info.uid,info.username,pollikos_getpid(),(unsigned long)info.session,info.rights,info.rights&USER_CAP_ADMIN?"administrator":"user");return 0;}
    if(!strcmp(name,"free")) {
        printf("RAM KiB: total=%lu used=%lu free=%lu (managed physical memory)\n",(unsigned long)(info.ram_bytes/1024),(unsigned long)((info.ram_bytes-info.ram_free_bytes)/1024),(unsigned long)(info.ram_free_bytes/1024));return 0;
    }
    if(!strcmp(name,"df")) {
        printf("PollikFS KiB: total=%lu used=%lu free=%lu free-inodes=%u %s\n",(unsigned long)info.fs_blocks*info.fs_block_size/1024,(unsigned long)(info.fs_blocks-info.fs_free_blocks)*info.fs_block_size/1024,(unsigned long)info.fs_free_blocks*info.fs_block_size/1024,info.fs_free_inodes,info.fs_readonly?"read-only":"read-write");
        printf("Physical data disk: %lu bytes; filesystem capacity is separate\n",(unsigned long)info.disk_bytes);return 0;
    }
    printf("Uptime: %lu days %02lu:%02lu:%02lu\n",(unsigned long)(info.uptime_ms/86400000),(unsigned long)(info.uptime_ms/3600000%24),(unsigned long)(info.uptime_ms/60000%60),(unsigned long)(info.uptime_ms/1000%60));
    if(!strcmp(name,"uptime"))return 0;
    line_out("PollikOS v0.0.001 x86_64 | BIOS | PollikFS v2 | native userspace");
    printf("User: %s | role: %s | processes: %u/%u\n",info.username,info.rights&USER_CAP_ADMIN?"administrator":"user",info.process_count,info.process_limit);
    printf("RAM: %lu MiB managed, %lu MiB free\n",(unsigned long)(info.ram_bytes/1048576),(unsigned long)(info.ram_free_bytes/1048576));
    printf("Filesystem: %lu KiB capacity, %lu KiB free, %s\n",(unsigned long)info.fs_blocks*info.fs_block_size/1024,(unsigned long)info.fs_free_blocks*info.fs_block_size/1024,info.fs_readonly?"read-only":"read-write");
    unsigned a,b,c,d,max;__asm__ volatile("cpuid":"=a"(max),"=b"(b),"=c"(c),"=d"(d):"a"(0x80000000u),"c"(0));
    if(max>=0x80000004u){char brand[49];for(unsigned i=0;i<3;i++){__asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0x80000002u+i),"c"(0));memcpy(brand+i*16,&a,4);memcpy(brand+i*16+4,&b,4);memcpy(brand+i*16+8,&c,4);memcpy(brand+i*16+12,&d,4);}brand[48]=0;printf("CPU: %s\n",brand);}
    return 0;
}
static int builtin_device(int argc,const char *const *argv) {
    const char *name=argv[0];long result;
    if(!strcmp(name,"reboot") || !strcmp(name,"shutdown") || !strcmp(name,"poweroff")) {
        if(argc!=1){line_out("Usage: reboot | shutdown | poweroff");return 2;}
        result=pollikos_device(USER_DEVICE_POWER,!strcmp(name,"reboot"));return command_error(name,result);
    }
    if(!strcmp(name,"volume")) {
        if(argc==1){printf("Volume: %ld%%\n",(long)pollikos_device(USER_DEVICE_VOLUME_GET,0));return 0;}
        unsigned long value;if(argc!=2 || !number(argv[1],100,&value)){line_out("Usage: volume [0..100]");return 2;}
        result=pollikos_device(USER_DEVICE_VOLUME_SET,value);
    } else if(!strcmp(name,"airplane")) {
        if(argc==1){printf("Airplane mode: %s\n",pollikos_device(USER_DEVICE_AIRPLANE_GET,0)>0?"on":"off");return 0;}
        if(argc!=2 || (strcmp(argv[1],"on") && strcmp(argv[1],"off"))){line_out("Usage: airplane [on|off]");return 2;}
        result=pollikos_device(USER_DEVICE_AIRPLANE_SET,!strcmp(argv[1],"on"));
    } else if(!strcmp(name,"sound")) {
        if(argc!=1){line_out("Usage: sound");return 2;}result=pollikos_device(USER_DEVICE_TEST_TONE,0);
    } else {
        if(argc!=1){line_out("Usage: devices | net | display");return 2;}
        long caps=pollikos_device(USER_DEVICE_CAPS,0);if(caps<0)return command_error(name,caps);
        if(!strcmp(name,"net")){printf("Network: %s, airplane=%s, TX=%ld RX=%ld packets\n",pollikos_device(USER_DEVICE_CONNECTED,0)>0?"connected":"disconnected",pollikos_device(USER_DEVICE_AIRPLANE_GET,0)>0?"on":"off",(long)pollikos_device(USER_DEVICE_TX_PACKETS,0),(long)pollikos_device(USER_DEVICE_RX_PACKETS,0));return 0;}
        printf("Display: %ldx%ld, %ld bpp, pitch=%ld bytes\n",(long)pollikos_device(USER_DEVICE_FB_WIDTH,0),(long)pollikos_device(USER_DEVICE_FB_HEIGHT,0),(long)pollikos_device(USER_DEVICE_FB_BPP,0),(long)pollikos_device(USER_DEVICE_FB_PITCH,0));
        if(strcmp(name,"display"))printf("Devices: Ethernet=%s WiFi=%s AC97=%s PC-speaker=%s; radio stacks unavailable\n",caps&1?"yes":"no",caps&2?"yes":"no",caps&8?"yes":"no",caps&16?"yes":"no");return 0;
    }
    if(result<0)return command_error(name,result);printf("%s: applied\n",name);return 0;
}
static int builtin_admin(int argc,const char *const *argv) {
    int interactive=!strcmp(argv[0],"admin") || (argc==2&&!strcmp(argv[1],"-i"));
    if((!strcmp(argv[0],"admin") && argc!=1) || (interactive && argc>2) || (!interactive && argc<2)){line_out("Usage: admin | sudo -i | sudo COMMAND [ARGS]");return 2;}
    long rights=pollikos_session_control(USER_SESSION_RIGHTS);
    if(rights<0)return command_error("rights",0);
    if(!interactive && rights>=0 && (rights&USER_CAP_ADMIN))return run_command(argc-1,argv+1);
    if(!(rights&USER_CAP_ADMIN)) {long result=pollikos_session_elevate("/bin/pollish");if(result<0)return command_error("administrator authentication",0);}
    const char *child[ARG_MAX+3];unsigned used=0;child[used++]="pollish";
    if(interactive)child[used++]="--admin-shell";
    else {child[used++]="-c";for(int i=1;i<argc;i++)child[used++]=argv[i];}
    child[used]=NULL;
    long pid=pollikos_spawn_rights("/bin/pollish",child,NULL,USER_CAP_ADMIN_ALL);
    if(pid<0)return command_error("admin spawn",pid);
    (void)setpgid((pid_t)pid,(pid_t)pid);(void)pollikos_foreground(pid);
    pollikos_wait_t status;long result=pollikos_waitpid(pid,&status);(void)pollikos_foreground(0);
    return result<0?command_error("admin wait",result):wait_code(&status);
}
static int builtin_sleep(int argc,const char *const *argv) {
    unsigned long seconds;if(argc!=2 || !number(argv[1],86400,&seconds)){line_out("Usage: sleep SECONDS (0..86400)");return 2;}
    long result=pollikos_sleep_ms(seconds*1000);return result<0?command_error("sleep",result):0;
}
static int builtin_stat(int argc,const char *const *argv) {
    if(argc!=2){line_out("Usage: stat PATH");return 2;}struct stat st;
    if(stat(argv[1],&st)<0)return command_error("stat",0);
    printf("%s: inode=%lu type=%s bytes=%lu modified-ticks=%lu\n",argv[1],st.st_ino,S_ISDIR(st.st_mode)?"directory":"file",st.st_size,st.st_mtime);return 0;
}
static int builtin_cp(int argc,const char *const *argv) {
    if(argc!=3){line_out("Usage: cp SOURCE DESTINATION (regular files)");return 2;}
    int source=open(argv[1],O_RDONLY);if(source<0)return command_error("cp",0);
    struct stat st,old;int failed=fstat(source,&st)<0;
    if(!failed && !S_ISREG(st.st_mode)){errno=EISDIR;failed=1;}
    if(!failed && stat(argv[2],&old)==0 && old.st_ino==st.st_ino){errno=EINVAL;failed=1;}
    char temporary[USER_PATH_MAX];int n=snprintf(temporary,sizeof(temporary),"%s.copy-XXXXXX",argv[2]);
    if(n<0 || (size_t)n>=sizeof(temporary)){errno=ENAMETOOLONG;failed=1;}
    int target=failed?-1:mkstemp(temporary);if(target<0)failed=1;
    char buffer[4096];ssize_t got=0;
    while(!failed && (got=read(source,buffer,sizeof(buffer)))>0) {
        ssize_t used=0;while(used<got){ssize_t wrote=write(target,buffer+used,(size_t)(got-used));if(wrote<=0){if(!wrote)errno=EIO;failed=1;break;}used+=wrote;}
    }
    if(got<0)failed=1;int saved=errno;
    if(close(source)<0 && !failed){failed=1;saved=errno;}
    if(target>=0 && close(target)<0 && !failed){failed=1;saved=errno;}
    if(!failed){long result=pollikos_rename_replace(temporary,argv[2]);if(result<0){failed=1;__pollikos_fail(result);saved=errno;}}
    if(failed){if(target>=0)unlink(temporary);errno=saved;return command_error("cp",0);}return 0;
}
static int builtin_count(int argc,const char *const *argv) {
    int head=!strcmp(argv[0],"head");unsigned long limit=10;int at=1;
    if(head && argc>2 && !strcmp(argv[1],"-n")){if(!number(argv[2],1000000,&limit)){line_out("Invalid line count");return 2;}at=3;}
    if(argc>at+1){line_out("Usage: wc [FILE] | head [-n LINES] [FILE]");return 2;}
    int fd=argc>at?open(argv[at],O_RDONLY):0;if(fd<0)return command_error(argv[0],0);
    unsigned long bytes=0,words=0,lines=0;int in_word=0,failed=0;char buffer[4096];ssize_t n=0;
    while((!head || lines<limit) && (n=read(fd,buffer,sizeof(buffer)))>0) {
        size_t used=0;for(;used<(size_t)n;used++){unsigned char ch=buffer[used];bytes++;if(ch=='\n')lines++;int ws=ch==' '||ch=='\n'||ch=='\t'||ch=='\r';if(!ws && !in_word)words++;in_word=!ws;if(head && lines>=limit){used++;break;}}
        if(head)emit(buffer,used);
    }
    if(!head || lines<limit){if(n<0)failed=1;}
    if(fd!=0 && close(fd)<0)failed=1;
    if(failed)return command_error(argv[0],0);
    if(!head)printf("%lu %lu %lu%s%s\n",lines,words,bytes,argc>at?" ":"",argc>at?argv[at]:"");return 0;
}
static int walk_find(const char *path,unsigned depth) {
    struct stat st;if(stat(path,&st)<0)return command_error("find",0);line_out(path);
    if(!S_ISDIR(st.st_mode))return 0;
    if(depth==32){line_out("find: directory depth limit (32)");return 1;}
    DIR *dir=opendir(path);if(!dir)return command_error("find",0);int failed=0;struct dirent *entry;
    while((entry=readdir(dir))) {
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
        char next[USER_PATH_MAX];int n=snprintf(next,sizeof(next),"%s%s%s",path,!strcmp(path,"/")?"":"/",entry->d_name);
        if(n<0 || (size_t)n>=sizeof(next)){line_out("find: path too long");failed=1;continue;}
        if(walk_find(next,depth+1))failed=1;
    }
    if(closedir(dir)<0)failed=1;return failed;
}
static int builtin_find(int argc,const char *const *argv) {if(argc>2){line_out("Usage: find [PATH]");return 2;}return walk_find(argc==2?argv[1]:".",0);}
static int builtin_kill(int argc,const char *const *argv) {
    unsigned long pid,signal=SIGTERM;int at=1;
    if(argc==3 && argv[1][0]=='-'){if(!number(argv[1]+1,NSIG-1,&signal)){line_out("Invalid signal");return 2;}at=2;}
    if(argc!=at+1 || !number(argv[at],2147483647,&pid) || !pid){line_out("Usage: kill [-SIGNAL] PID");return 2;}
    return kill((pid_t)pid,(int)signal)<0?command_error("kill",0):0;
}
static int builtin_fetch(int argc,const char *const *argv) {
    if(argc!=2){line_out("Usage: fetch URL (HTTP/HTTPS GET to stdout)");return 2;}
    long handle=pollikos_http_open(argv[1]);if(handle<0)return command_error("fetch",handle);
    char buffer[4096];long n;int failed=0;
    while((n=pollikos_http_read(handle,buffer,sizeof(buffer)))!=0){if(n==-USER_EAGAIN){pollikos_sleep_ms(10);continue;}if(n<0){failed=1;break;}emit(buffer,(size_t)n);}
    long status=pollikos_http_status(handle);pollikos_http_close(handle);
    if(failed)return command_error("fetch",n);
    if(status>=400){fprintf(stderr,"fetch: HTTP %ld\n",status);return 1;}return 0;
}
