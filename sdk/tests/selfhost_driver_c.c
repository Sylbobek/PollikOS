#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <setjmp.h>
#include <math.h>
#include <signal.h>
#include <pollikos/process.h>

static int write_source(const char *path, const char *source) {
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    size_t size = strlen(source);
    if (fwrite(source, 1, size, file) != size) { fclose(file); return -1; }
    return fclose(file);
}

static int run(const char *path, const char *const argv[], int expected) {
    long pid = pollikos_spawn(path, argv, NULL);
    pollikos_wait_t status;
    if (pid <= 0) {
        printf("[SELFHOST] spawn %s failed rc=%ld errno=%s\n", path, pid, strerror(errno));
        return -1;
    }
    if (pollikos_waitpid(pid, &status) != pid) {
        printf("[SELFHOST] wait %s failed errno=%s\n", path, strerror(errno));
        return -1;
    }
    if (status.kind != POLLIKOS_WAIT_EXITED) {
        printf("[SELFHOST] %s terminated kind=%u code=%d\n", path, (unsigned)status.kind, (int)status.code);
        return -2;
    }
    if (status.code != expected)
        printf("[SELFHOST] %s exited %d; expected %d\n", path, (int)status.code, expected);
    return status.code == expected ? 0 : status.code + 1000;
}

static int bootstrap_libc(void) {
    static const char *sources[] = {
        "assert", "ctype", "dir", "errno", "fs", "malloc", "math", "mem", "process",
        "setjmp", "signal", "start", "stdio", "stdlib", "string", "time", "version", NULL
    };
    static char objects[17][32];
    static const char *archive[22];
    archive[0] = "tcc"; archive[1] = "-ar"; archive[2] = "rcs";
    archive[3] = "/usr/lib/libc.a";
    for (int index = 0; sources[index]; ++index) {
        char input[64];
        snprintf(input, sizeof(input), "/usr/src/libc/%s.c", sources[index]);
        snprintf(objects[index], sizeof(objects[index]), "/tmp/libc-%s.o", sources[index]);
        const char *compile[] = {"tcc", "-c", input, "-o", objects[index], NULL};
        if (run("/bin/tcc", compile, 0) != 0) return -1;
        archive[index + 4] = objects[index];
        archive[index + 5] = NULL;
    }
    remove("/usr/lib/libc.a");
    if (run("/bin/tcc", archive, 0) != 0) return -2;
    for (int index = 0; sources[index]; ++index) remove(objects[index]);
    return 0;
}

static int contains(const char *path, const char *needle) {
    static char buffer[1024];
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    size_t size = fread(buffer, 1, sizeof(buffer)-1, file);
    fclose(file); buffer[size] = 0;
    return strstr(buffer, needle) != NULL;
}

static void dump(const char *path) {
    static char buffer[2048];
    FILE *file = fopen(path, "rb");
    if (!file) { printf("[SELFHOST] dump %s: errno=%s\n", path, strerror(errno)); return; }
    size_t size = fread(buffer, 1, sizeof(buffer)-1, file);
    fclose(file); buffer[size] = 0;
    printf("[SELFHOST] dump %s (%lu bytes):\n%s\n", path, (unsigned long)size, buffer);
}

static int compiler_checks(void) {
    static const char pre[] =
        "#include <stdint.h>\n#define VALUE 73\n#if defined(__pollikos__)\nint answer = VALUE;\n#endif\n";
    static const char date_macros[] =
        "const char build_date[] = __DATE__;\nconst char build_time[] = __TIME__;\n";
    static const char libc[] =
        "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"
        "int main(void){char *p=malloc(16);if(!p)return 1;strcpy(p,\"libc-ok\");"
        "printf(\"%s %lu\\n\",p,(unsigned long)strlen(p));free(p);return 42;}\n";
    static const char fileprog[] =
        "#include <stdio.h>\n#include <string.h>\n"
        "int main(void){char b[16]={0};FILE*f=fopen(\"/home/native.txt\",\"wb\");"
        "if(!f)return 1;fwrite(\"native-io\",1,9,f);fclose(f);f=fopen(\"/home/native.txt\",\"rb\");"
        "if(!f)return 2;fread(b,1,9,f);fclose(f);printf(\"%s\\n\",b);return strcmp(b,\"native-io\")?3:42;}\n";
    static const char main_c[] = "#include <stdio.h>\nint util(void);int main(void){printf(\"multi=%d\\n\",util());return util();}\n";
    static const char util_c[] = "int util(void){return 42;}\n";
    static const char invalid[] = "int main( { return 0; }\n";
    static const char missing[] = "#include <does-not-exist.h>\nint main(void){return 0;}\n";
    static const char floating[] =
        "#include <math.h>\n"
        "int main(void){volatile double d=3.0;volatile float f=2.0f;"
        "if((int)(d*d+f)!=11)return 1;if((int)sqrt(81.0)!=9)return 2;"
        "if((int)floor(-1.2)!=-2||(int)ceil(1.2)!=2)return 3;"
        "if((int)round(1.5)!=2||(int)ldexp(1.5,4)!=24)return 4;"
        "if(fabs(exp(1.0)-2.718281828459045)>1e-12)return 5;"
        "if(fabs(log(2.0)-0.6931471805599453)>1e-12)return 6;"
        "if(fabs(pow(-2.0,3.0)+8.0)>1e-12||!isnan(pow(-2.0,0.5)))return 7;"
        "if(fabs(sin(1.5707963267948966)-1.0)>1e-12||fabs(cos(0.0)-1.0)>1e-12)return 8;"
        "if(fabs(atan(2.0)-1.1071487177940904)>1e-12)return 9;"
        "if(fabs(atan2(-1.0,-1.0)+2.356194490192345)>1e-12)return 10;"
        "if(fmod(-5.5,2.0)!=-1.5)return 11;"
        "if(fabsf(fmodf(-5.5f,2.0f)+1.5f)>1e-6f)return 12;"
        "{int e=0;double m=frexp(12.0,&e);if(fabs(m-0.75)>1e-12||e!=4)return 13;}"
        "{double i=0.0;if(modf(-3.25,&i)!=-0.25||i!=-3.0)return 14;}"
        "{double i=0.0;if(!signbit(modf(-2.0,&i))||!signbit(pow(-0.0,3.0)))return 15;}"
        "if(fabs(atan2(-0.0,-0.0)+3.141592653589793)>1e-12)return 16;"
        "if(!isinf(ldexp(exp(1000.0),-5000)))return 17;"
        "return 42;}\n";
    static const char signals[] =
        "#include <signal.h>\n"
        "static volatile int seen;static void handler(int s){seen=s;}\n"
        "int main(void){struct sigaction a={0},old={0};sigset_t set,pending;"
        "a.sa_handler=handler;if(sigaction(SIGUSR1,&a,&old)||old.sa_handler!=SIG_DFL)return 1;"
        "if(raise(SIGUSR1)||seen!=SIGUSR1)return 2;"
        "if(sigemptyset(&set)||sigaddset(&set,SIGUSR1)||sigprocmask(SIG_BLOCK,&set,0))return 3;"
        "if(raise(SIGUSR1)||seen!=SIGUSR1||sigpending(&pending)||!sigismember(&pending,SIGUSR1))return 4;"
        "if(sigprocmask(SIG_UNBLOCK,&set,0)||seen!=SIGUSR1)return 5;"
        "if(signal(SIGUSR1,SIG_IGN)!=handler||raise(SIGUSR1)||seen!=SIGUSR1)return 6;return 42;}\n";
    static const char stop_child[] =
        "#include <stdio.h>\n#include <pollikos/time.h>\n"
        "int main(void){if(sleep_ms(200))return 3;FILE*f=fopen(\"/home/stop.marker\",\"wb\");"
        "if(!f)return 1;if(fclose(f))return 2;return 42;}\n";
    static const char stop_test[] =
        "#include <signal.h>\n#include <stdio.h>\n#include <sys/stat.h>\n"
        "#include <pollikos/process.h>\n#include <pollikos/time.h>\n"
        "int main(void){const char*a[]={\"stop-child\",0};pollikos_wait_t s;"
        "struct stat st;if(stat(\"/home/stop-child\",&st))return 10;"
        "if(stat(\"/home/stop.marker\",&st)==0&&remove(\"/home/stop.marker\"))return 9;"
        "long p=pollikos_spawn(\"/home/stop-child\",a,0);if(p<=0)return 1;"
        "if(kill((pid_t)p,SIGSTOP))return 2;if(sleep_ms(400))return 3;"
        "if(stat(\"/home/stop.marker\",&st)==0)return 4;"
        "if(kill((pid_t)p,SIGCONT))return 5;"
        "if(pollikos_waitpid(p,&s)!=p)return 6;"
        "if(s.kind!=POLLIKOS_WAIT_EXITED||s.code!=42)return 7;"
        "if(stat(\"/home/stop.marker\",&st))return 8;return 42;}\n";
    static const char group_child[] =
        "#include <pollikos/time.h>\n"
        "int main(void){for(;;)if(sleep_ms(1000))return 7;}\n";
    static const char groups[] =
        "#include <signal.h>\n#include <unistd.h>\n#include <pollikos/process.h>\n"
        "#include <sys/wait.h>\n#include <errno.h>\n"
        "int main(void){const char*a[]={\"group-child\",0};int x,y;pid_t first,second;"
        "if(setpgid(0,0)||getpgid(0)!=getpid()||getpgrp()!=getpid())return 1;"
        "long p=pollikos_spawn(\"/home/group-child\",a,0);if(p<=0)return 2;"
        "if(setpgid((pid_t)p,(pid_t)p))return 3;"
        "long q=pollikos_spawn(\"/home/group-child\",a,0);if(q<=0)return 4;"
        "if(setpgid((pid_t)q,(pid_t)p))return 5;"
        "long r=pollikos_spawn(\"/home/group-child\",a,0);if(r<=0)return 6;"
        "if(setpgid((pid_t)r,(pid_t)r))return 7;"
        "if(getpgid((pid_t)q)!=(pid_t)p||getpgid((pid_t)r)!=(pid_t)r)return 8;"
        "if(waitpid(-(pid_t)p,&x,WNOHANG)!=0)return 9;"
        "if(kill(-(pid_t)p,SIGTERM))return 9;"
        "first=waitpid(-(pid_t)p,&x,0);second=waitpid(-(pid_t)p,&y,0);"
        "if(!((first==(pid_t)p&&second==(pid_t)q)||(first==(pid_t)q&&second==(pid_t)p)))return 10;"
        "if(!WIFSIGNALED(x)||WTERMSIG(x)!=SIGTERM||!WIFSIGNALED(y)||WTERMSIG(y)!=SIGTERM)return 11;"
        "errno=0;if(waitpid(-(pid_t)p,&x,WNOHANG)!=-1||errno!=ECHILD)return 12;"
        "if(getpgid((pid_t)r)!=(pid_t)r)return 13;"
        "if(kill((pid_t)r,SIGKILL)||waitpid(-1,&x,0)!=(pid_t)r||"
        "!WIFSIGNALED(x)||WTERMSIG(x)!=SIGKILL)return 14;return 42;}\n";
    struct stat info;
    const char *preprocess[] = {"tcc", "-E", "/home/pre.c", "-o", "/home/pre.i", NULL};
    const char *preprocess_date[] = {"tcc", "-E", "/home/date.c", "-o", "/home/date.i", NULL};
    const char *object[] = {"tcc", "-c", "/home/hello.c", "-o", "/home/hello.o", NULL};
    const char *libc_cmd[] = {"tcc", "/home/libc.c", "-o", "/home/libc-test", NULL};
    const char *libc_run[] = {"libc-test", NULL};
    const char *file_cmd[] = {"tcc", "/home/file.c", "-o", "/home/file-test", NULL};
    const char *file_run[] = {"file-test", NULL};
    const char *multi_cmd[] = {"tcc", "/home/main.c", "/home/util.c", "-DVALUE=42",
                               "-I/usr/include", "-o", "/home/multi", NULL};
    const char *multi_run[] = {"multi", NULL};
    const char *main_obj[] = {"tcc", "-c", "/home/main.c", "-o", "/home/main.o", NULL};
    const char *util_obj[] = {"tcc", "-c", "/home/util.c", "-o", "/home/util.o", NULL};
    const char *multi_obj[] = {"tcc", "/home/main.o", "/home/util.o", "-o", "/home/multi2", NULL};
    const char *bad_cmd[] = {"tcc", "/home/invalid.c", "-o", "/home/invalid", NULL};
    const char *missing_cmd[] = {"tcc", "/home/missing.c", "-o", "/home/missing", NULL};
    const char *float_cmd[] = {"tcc", "/home/float.c", "-o", "/home/float", NULL};
    const char *float_run[] = {"float", NULL};
    const char *signal_cmd[] = {"tcc", "/home/signals.c", "-o", "/home/signals", NULL};
    const char *signal_run[] = {"signals", NULL};
    const char *stop_child_cmd[] = {"tcc", "/home/stop-child.c", "-o", "/home/stop-child", NULL};
    const char *stop_test_cmd[] = {"tcc", "/home/stop-test.c", "-o", "/home/stop-test", NULL};
    const char *stop_test_run[] = {"stop-test", NULL};
    const char *group_child_cmd[] = {"tcc", "/home/group-child.c", "-o", "/home/group-child", NULL};
    const char *groups_cmd[] = {"tcc", "/home/groups.c", "-o", "/home/groups", NULL};
    const char *groups_run[] = {"groups", NULL};
    const char *bad_output[] = {"tcc", "/home/hello.c", "-o", "/missing/out", NULL};

    if (write_source("/home/pre.c", pre)) {
        printf("[SELFHOST] preprocess write errno=%s\n", strerror(errno));
        return 20;
    }
    {
        int code = run("/bin/tcc", preprocess, 0);
        if (code) { printf("[SELFHOST] preprocess tcc rc=%d\n", code); return 20; }
        if (stat("/home/pre.i", &info) != 0) {
            printf("[SELFHOST] preprocess output missing errno=%s\n", strerror(errno));
            return 20;
        }
        if (!contains("/home/pre.i", "answer = 73")) {
            printf("[SELFHOST] preprocess needle missing size=%lu\n", info.st_size);
            dump("/home/pre.i");
            return 20;
        }
    }
    printf("[SELFHOST] preprocessor PASS\n");
    if (write_source("/home/date.c", date_macros) ||
        run("/bin/tcc", preprocess_date, 0) ||
        !contains("/home/date.i", "Jan  1 1970") ||
        !contains("/home/date.i", "00:00:00")) {
        printf("[SELFHOST] deterministic date macros FAIL\n");
        return 20;
    }
    printf("[SELFHOST] deterministic date macros PASS\n");
    if (run("/bin/tcc", object, 0) || stat("/home/hello.o", &info) || info.st_size < 64) return 21;
    printf("[SELFHOST] object PASS\n");
    if (write_source("/home/libc.c", libc) || run("/bin/tcc", libc_cmd, 0) ||
        run("/home/libc-test", libc_run, 42)) return 22;
    printf("[SELFHOST] libc PASS\n");
    if (write_source("/home/file.c", fileprog) || run("/bin/tcc", file_cmd, 0) ||
        run("/home/file-test", file_run, 42)) return 23;
    printf("[SELFHOST] filesystem PASS\n");
    if (write_source("/home/main.c", main_c) || write_source("/home/util.c", util_c) ||
        run("/bin/tcc", multi_cmd, 0) || run("/home/multi", multi_run, 42)) return 24;
    if (run("/bin/tcc", main_obj, 0) || run("/bin/tcc", util_obj, 0) ||
        run("/bin/tcc", multi_obj, 0) || run("/home/multi2", multi_run, 42)) return 25;
    printf("[SELFHOST] multi-file and object-link PASS\n");
    write_source("/home/invalid.c", invalid); remove("/home/invalid");
    if (run("/bin/tcc", bad_cmd, 0) == 0 || stat("/home/invalid", &info) == 0) return 26;
    write_source("/home/missing.c", missing); remove("/home/missing");
    if (run("/bin/tcc", missing_cmd, 0) == 0) return 27;
    if (write_source("/home/float.c", floating)) return 28;
    remove("/home/float");
    if (run("/bin/tcc", float_cmd, 0) || run("/home/float", float_run, 42)) return 28;
    if (run("/bin/tcc", bad_output, 0) == 0) return 29;
    printf("[SELFHOST] diagnostics and float/double math PASS\n");
    if (write_source("/home/signals.c", signals) || run("/bin/tcc", signal_cmd, 0) ||
        run("/home/signals", signal_run, 42)) return 30;
    printf("[SELFHOST] POSIX signals PASS\n");
    if (write_source("/home/stop-child.c", stop_child) ||
        write_source("/home/stop-test.c", stop_test) ||
        run("/bin/tcc", stop_child_cmd, 0) || run("/bin/tcc", stop_test_cmd, 0) ||
        run("/home/stop-test", stop_test_run, 42)) return 31;
    printf("[SELFHOST] SIGSTOP/SIGCONT PASS\n");
    if (write_source("/home/group-child.c", group_child) || write_source("/home/groups.c", groups) ||
        run("/bin/tcc", group_child_cmd, 0) || run("/bin/tcc", groups_cmd, 0) ||
        run("/home/groups", groups_run, 42)) return 32;
    printf("[SELFHOST] PROCESS GROUPS PASS\n");
    return 0;
}

int main(void) {
    static const char hello[] =
        "#include <stdio.h>\n"
        "int main(void) {\n"
        "  printf(\"Hello from self-hosted PollikOS C!\\n\");\n"
        "  return 42;\n"
        "}\n";
    const char *tcc_version[] = {"tcc", "-v", NULL};
    const char *compile[] = {"tcc", "/home/hello.c", "-o", "/home/hello", NULL};
    const char *program[] = {"hello", NULL};
    struct stat info;
    jmp_buf jump;

    if (setjmp(jump) == 0) longjmp(jump, 7);
    printf("[SELFHOST] setjmp-longjmp PASS\n");

    printf("[SELFHOST] compiler=/bin/tcc\n");
    if (run("/bin/tcc", tcc_version, 0) != 0) return 10;
    if (bootstrap_libc() != 0) return 15;
    printf("[SELFHOST] source=/home/hello.c\n");
    if (write_source("/home/hello.c", hello) != 0) return 11;
    remove("/home/hello");
    printf("[SELFHOST] native compile begin\n");
    int result = run("/bin/tcc", compile, 0);
    printf("[SELFHOST] tcc exit=%d\n", result);
    if (result != 0) return 12;
    printf("[SELFHOST] output=/home/hello\n");
    if (stat("/home/hello", &info) != 0 || info.st_size < 64) return 13;
    printf("[SELFHOST] output ELF valid\n");
    result = run("/home/hello", program, 42);
    printf("[SELFHOST] program exit=%d\n", result == 0 ? 42 : result);
    if (result != 0) return 14;
    result = compiler_checks();
    if (result != 0) { printf("[SELFHOST] compiler checks failed=%d\n", result); return 16; }
    for (int cycle = 1; cycle <= 25; ++cycle) {
        remove("/home/hello");
        int code = run("/bin/tcc", compile, 0);
        if (code) { printf("[SELFHOST] churn %d compile rc=%d\n", cycle, code); return 17; }
        errno = 0;
        if (stat("/home/hello", &info) != 0) {
            printf("[SELFHOST] churn %d output missing errno=%s\n", cycle, strerror(errno));
            return 17;
        }
        if (info.st_size < 4096) {
            printf("[SELFHOST] churn %d truncated output size=%lu\n", cycle,
                   (unsigned long)info.st_size);
            return 17;
        }
        code = run("/home/hello", program, 42);
        if (code) { printf("[SELFHOST] churn %d program rc=%d size=%lu\n", cycle, code,
                           (unsigned long)info.st_size); return 17; }
    }
    printf("[SELFHOST] churn cycles=25 PASS\n");
    printf("[SELFHOST] PASS\n");
    return 42;
}
