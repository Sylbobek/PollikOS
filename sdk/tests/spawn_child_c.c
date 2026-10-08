/* C6 child fixture: verifies its startup ABI, environment and inherited state,
 * then exits with the mode-selected status. Built by the SDK only. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pollikos/process.h>
#include <pollikos/fs.h>
#include <pollikos/time.h>
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "echo";
    if (strcmp(mode, "echo") == 0) {
        const char *test = getenv("TEST");
        printf("[child] pid=%d argc=%d argv2=%s argv3=%s TEST=%s\n",
               getpid(), argc, argc > 2 ? argv[2] : "-", argc > 3 ? argv[3] : "-",
               test ? test : "(none)");
        if (argc != 4) return 1;
        if (strcmp(argv[2], "first") != 0 || strcmp(argv[3], "second") != 0) return 2;
        if (!test || strcmp(test, "pollikos") != 0) return 3;
        return 42;
    }
    if (strcmp(mode, "exitcode") == 0)
        return argc > 2 ? atoi(argv[2]) : 1;
    if (strcmp(mode, "sleep") == 0) {
        if (sleep_ms(argc > 2 ? (unsigned)atoi(argv[2]) : 10) != 0) return 1;
        return 5;
    }
    if (strcmp(mode, "fault") == 0) {
        *(volatile unsigned long *)(uintptr_t)0 = 1;
        return 1;
    }
    if (strcmp(mode, "spin") == 0) {
        volatile unsigned long counter = 0;
        for (;;) counter++;
    }
    if (strcmp(mode, "checkenv") == 0) {
        const char *test = getenv("TEST");
        if (!test || strcmp(test, "pollikos") != 0) return 1;
        if (setenv("TEST", "changed", 1) != 0) return 2;
        test = getenv("TEST");
        if (!test || strcmp(test, "changed") != 0) return 3;
        printf("[child] environment inherited and locally mutated\n");
        return 42;
    }
    if (strcmp(mode, "checkcwd") == 0) {
        char cwd[128];
        const char *expected = argc > 2 ? argv[2] : "/etc";
        if (!getcwd(cwd, sizeof(cwd)) || strcmp(cwd, expected) != 0) {
            printf("[child] cwd mismatch got=%s want=%s\n", cwd, expected);
            return 1;
        }
        if (chdir("/") != 0) return 2;
        printf("[child] inherited cwd %s then changed to /\n", expected);
        return 42;
    }
    if (strcmp(mode, "checkfd") == 0) {
        char buffer[4];
        long count = read(3, buffer, sizeof(buffer));
        if (count != 4 || memcmp(buffer, "Alph", 4) != 0) {
            printf("[child] fd 3 read failed count=%ld\n", count);
            return 1;
        }
        printf("[child] inherited fd 3 read \"Alph\"\n");
        return 42;
    }
    if (strcmp(mode, "checkclosedfd") == 0) {
        int fd = argc > 2 ? atoi(argv[2]) : -1;
        char byte;
        errno = 0;
        long count = fd >= 0 ? read(fd, &byte, 1) : -2;
        if(count!=-1 || errno!=EBADF) printf("[child] close-on-spawn fd=%d count=%ld errno=%d\n",fd,count,errno);
        return count == -1 && errno == EBADF ? 42 : 1;
    }
    if (strcmp(mode, "pipe_source") == 0) {
        static const char payload[] = "CLOEXEC_PIPE_EOF\n";
        return write(1, payload, sizeof(payload) - 1) == sizeof(payload) - 1 ? 0 : 1;
    }
    if (strcmp(mode, "pipe_relay") == 0) {
        char buffer[32];
        long total = 0;
        for (;;) {
            long count = read(0, buffer, sizeof(buffer));
            if (count < 0) return 1;
            if (!count) break;
            if (write(1, buffer, (size_t)count) != count) return 2;
            total += count;
        }
        return total == (long)strlen("CLOEXEC_PIPE_EOF\n") ? 0 : 3;
    }
    if (strcmp(mode, "pipe_consume") == 0) {
        static const char expected[] = "CLOEXEC_PIPE_EOF\n";
        char buffer[32];
        size_t total = 0;
        for (;;) {
            long count = read(0, buffer, sizeof(buffer));
            if (count < 0) return 1;
            if (!count) break;
            if (total + (size_t)count > sizeof(expected) - 1) return 2;
            if (memcmp(buffer, expected + total, (size_t)count) != 0) return 3;
            total += (size_t)count;
        }
        return total == sizeof(expected) - 1 ? 42 : 4;
    }
    if (strcmp(mode, "branch") == 0) {
        const char *arguments[] = {"spawn_child_c", "exitcode", "21", 0};
        long child = pollikos_spawn("/bin/spawn_child_c", arguments, 0);
        if (child < 0) return 1;
        pollikos_wait_t status;
        if (pollikos_waitpid(child, &status) != child) return 2;
        if (status.kind != POLLIKOS_WAIT_EXITED || status.code != 21) return 3;
        return 21;
    }
    if (strcmp(mode, "orphan") == 0) {
        if (sleep_ms(40) != 0) return 1;
        return 33;
    }
    printf("[child] unknown mode %s\n", mode);
    return 1;
}
