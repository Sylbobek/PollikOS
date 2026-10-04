/* C6 parent fixture: spawns ELF64 children, waits without busy-spinning and
 * inspects typed termination records. Built by the SDK only. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pollikos/process.h>
#include <pollikos/fs.h>
#include <pollikos/time.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
static int failures;
#define CHECK(condition, name) \
    do { if (!(condition)) { printf("[C6] FAIL: %s (errno=%s)\n", name, strerror(errno)); ++failures; } } while (0)
static const char *test_env[] = {"TEST=pollikos", 0};
static long spawn_child(const char *mode, const char *argument) {
    const char *arguments[4] = {"spawn_child_c", mode, argument, 0};
    return pollikos_spawn("/bin/spawn_child_c", arguments, test_env);
}
static int wait_exit(long child, int expected_code) {
    pollikos_wait_t status;
    long result = pollikos_waitpid(child, &status);
    CHECK(result == child, "waitpid returns the child pid");
    CHECK(status.version == POLLIKOS_WAIT_VERSION, "wait status version");
    CHECK(status.kind == POLLIKOS_WAIT_EXITED, "normal exit kind");
    CHECK(status.code == expected_code, "normal exit code");
    return failures ? 1 : 0;
}
static int mode_basic(void) {
    printf("[parent] pid=%d spawning child\n", getpid());
    long child = pollikos_spawn("/bin/spawn_child_c",
        (const char *const[]){"spawn_child_c", "echo", "first", "second", 0}, test_env);
    CHECK(child > 0, "spawn returns child pid");
    if (child < 0) return 1;
    printf("[parent] child pid=%ld, waiting\n", child);
    if (wait_exit(child, 42)) return 1;
    printf("[parent] child exited 42 and parent continues\n");
    return failures ? 1 : 42;
}
static int mode_early(void) {
    long child = spawn_child("exitcode", "7");
    CHECK(child > 0, "early child spawn");
    if (child < 0) return 1;
    if (sleep_ms(60) != 0) return 1; /* child exits long before the wait */
    return wait_exit(child, 7) ? 1 : 42;
}
static int mode_block(void) {
    long child = spawn_child("sleep", "40");
    CHECK(child > 0, "blocking child spawn");
    if (child < 0) return 1;
    long before = pollikos_monotonic_ms();
    if (wait_exit(child, 5)) return 1;
    long elapsed = pollikos_monotonic_ms()-before;
    CHECK(elapsed >= 30, "wait blocked until the child exited");
    printf("[parent] blocking wait observed %ld ms\n", elapsed);
    return failures ? 1 : 42;
}
static int mode_multi(void) {
    const char *codes[] = {"11", "22", "33", "44"};
    long children[4];
    for (unsigned i = 0; i < 4; ++i) {
        children[i] = spawn_child("exitcode", codes[i]);
        CHECK(children[i] > 0, "multiple children spawn");
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (children[i] < 0) continue;
        pollikos_wait_t status;
        long result = pollikos_waitpid(children[i], &status);
        CHECK(result == children[i], "pid/status association");
        CHECK(status.kind == POLLIKOS_WAIT_EXITED && status.code == atoi(codes[i]),
              "per-child exit code");
    }
    return failures ? 1 : 42;
}
static int mode_fault(void) {
    long child = spawn_child("fault", 0);
    CHECK(child > 0, "faulting child spawn");
    if (child < 0) return 1;
    pollikos_wait_t status;
    long result = pollikos_waitpid(child, &status);
    CHECK(result == child, "faulting waitpid result");
    CHECK(status.kind == POLLIKOS_WAIT_FAULTED, "fault termination kind");
    CHECK(status.code == 14 && status.address == 0, "fault vector and address");
    printf("[parent] survived child fault: kind=%u vector=%d address=0x%lx\n",
           (unsigned)status.kind, (int)status.code, (unsigned long)status.address);
    return failures ? 1 : 42;
}
static int mode_killed(void) {
    long child = spawn_child("spin", 0);
    CHECK(child > 0, "spinning child spawn");
    if (child < 0) return 1;
    int fd = open("/mutation/c6.pid", O_WRONLY|O_CREAT|O_TRUNC);
    if (fd < 0) return 1;
    char text[32];
    int length = snprintf(text, sizeof(text), "%ld\n", child);
    if (write(fd, text, (size_t)length) != length) return 1;
    close(fd);
    pollikos_wait_t status;
    long result = pollikos_waitpid(child, &status);
    CHECK(result == child, "killed waitpid result");
    CHECK(status.kind == POLLIKOS_WAIT_KILLED, "kill termination kind");
    printf("[parent] child killed with reason=%d\n", (int)status.code);
    return failures ? 1 : 42;
}
static int mode_errors(void) {
    long child = pollikos_spawn("/bin/definitely-missing", 0, 0);
    CHECK(child == -(long)USER_ENOENT, "missing executable is ENOENT");
    child = pollikos_spawn("/bin/invalid", 0, 0);
    CHECK(child == -(long)USER_ENOEXEC, "invalid ELF is ENOEXEC");
    pollikos_wait_t status;
    child = pollikos_waitpid(getpid(), &status);
    CHECK(child == -(long)USER_ECHILD, "waiting for self is ECHILD");
    child = pollikos_waitpid(0x7fffffff, &status);
    CHECK(child == -(long)USER_ECHILD, "unknown pid is ECHILD");
    child = spawn_child("exitcode", "9");
    CHECK(child > 0, "error fixture child");
    if (child > 0) {
        if (pollikos_waitpid(child, &status) != child) return 1;
        if (pollikos_waitpid(child, &status) != -(long)USER_ECHILD) {
            CHECK(0, "double wait is ECHILD");
        }
    }
    return failures ? 1 : 42;
}
static int mode_env(void) {
    if (setenv("TEST", "pollikos", 1) != 0) return 1;
    long child = pollikos_spawn("/bin/spawn_child_c", (const char *const[]){"spawn_child_c", "checkenv", 0}, 0);
    CHECK(child > 0, "environment child spawn");
    if (child < 0) return 1;
    if (wait_exit(child, 42)) return 1;
    const char *test = getenv("TEST");
    CHECK(test && strcmp(test, "pollikos") == 0, "parent environment unchanged");
    return failures ? 1 : 42;
}
static int mode_cwd(void) {
    if (chdir("/etc") != 0) return 1;
    long child = spawn_child("checkcwd", "/etc");
    CHECK(child > 0, "cwd child spawn");
    if (child < 0) return 1;
    if (wait_exit(child, 42)) return 1;
    char cwd[128];
    CHECK(getcwd(cwd, sizeof(cwd)) && strcmp(cwd, "/etc") == 0, "parent cwd unchanged");
    if (chdir("/") != 0) return 1;
    return failures ? 1 : 42;
}
static int mode_fds(void) {
    int fd = open("/etc/read_test.txt", O_RDONLY);
    CHECK(fd == 3, "parent opens fd 3");
    if (fd != 3) return 1;
    long child = spawn_child("checkfd", 0);
    CHECK(child > 0, "descriptor child spawn");
    if (child < 0) { close(fd); return 1; }
    if (wait_exit(child, 42)) { close(fd); return 1; }
    char buffer[4];
    long count = read(fd, buffer, sizeof(buffer));
    CHECK(count == 4 && memcmp(buffer, "a fi", 4) == 0, "shared offset after child read");
    CHECK(close(fd) == 0, "parent closes inherited fd independently");
    return failures ? 1 : 42;
}
static long spawn_pipe_stage(const char *mode, int input_fd, int output_fd) {
    int saved_in = -1, saved_out = -1;
    long child = -1;
    if (input_fd >= 0) {
        saved_in = dup(0);
        if (saved_in < 0 || pollikos_set_cloexec(saved_in, 1) < 0 || dup2(input_fd, 0) < 0)
            goto restore;
    }
    if (output_fd >= 0) {
        saved_out = dup(1);
        if (saved_out < 0 || pollikos_set_cloexec(saved_out, 1) < 0 || dup2(output_fd, 1) < 0) {
            goto restore;
        }
    }
    const char *arguments[] = {"spawn_child_c", mode, 0};
    child = pollikos_spawn("/bin/spawn_child_c", arguments, 0);
restore:
    if (saved_out >= 0) { (void)dup2(saved_out, 1); close(saved_out); }
    if (saved_in >= 0) { (void)dup2(saved_in, 0); close(saved_in); }
    return child;
}
static int mode_cloexec(void) {
    errno = 0;
    CHECK(open("/etc/read_test.txt", O_RDONLY|0x2000) == -1 && errno == EINVAL,
          "unknown open flag remains EINVAL");

    int file = open("/etc/read_test.txt", O_RDONLY|O_CLOEXEC);
    CHECK(file >= 0, "O_CLOEXEC open succeeds");
    if (file < 0) return 1;
    CHECK(pollikos_set_cloexec(file, 1) == 1, "open flag sets per-descriptor close-on-spawn");
    char fd_text[12];
    snprintf(fd_text, sizeof(fd_text), "%d", file);
    const char *arguments[] = {"spawn_child_c", "checkclosedfd", fd_text, 0};
    long child = pollikos_spawn("/bin/spawn_child_c", arguments, 0);
    CHECK(child > 0, "close-on-spawn descriptor child starts");
    if (child > 0) (void)wait_exit(child, 42);
    CHECK(close(file) == 0, "close-on-spawn file closes in parent");
    if (failures) return 1;

    CHECK(pollikos_set_cloexec(2, 1) == 0, "standard descriptor accepts close-on-spawn");
    const char *stderr_arguments[] = {"spawn_child_c", "checkclosedfd", "2", 0};
    child = pollikos_spawn("/bin/spawn_child_c", stderr_arguments, 0);
    CHECK(child > 0, "standard descriptor child starts");
    if (child > 0) (void)wait_exit(child, 42);
    CHECK(pollikos_set_cloexec(2, 0) == 1, "parent standard descriptor flag remains set");
    if (failures) return 1;

    int first[2] = {-1, -1}, second[2] = {-1, -1};
    CHECK(pipe(first) == 0, "first pipeline pipe created");
    if (failures) return 1;
    CHECK(pipe(second) == 0, "second pipeline pipe created");
    if (failures) { close(first[0]); close(first[1]); return 1; }
    for (int index = 0; index < 2; ++index) {
        int *ends = index ? second : first;
        for (int end = 0; end < 2; ++end) {
            CHECK(pollikos_set_cloexec(ends[end], 1) == 0,
                  "pipe end close-on-spawn flag set");
            CHECK(pollikos_set_cloexec(ends[end], 1) == 1,
                  "pipe close-on-spawn state query");
        }
    }
    int duplicate = dup(first[0]);
    CHECK(duplicate >= 0, "duplicate close-on-spawn pipe end");
    if (duplicate >= 0) {
        CHECK(pollikos_set_cloexec(duplicate, 0) == 0,
              "dup clears close-on-spawn flag");
        CHECK(close(duplicate) == 0, "duplicate pipe end closes");
    }
    if (failures) {
        for (int index = 0; index < 2; ++index) {
            int *ends = index ? second : first;
            for (int end = 0; end < 2; ++end) (void)close(ends[end]);
        }
        return 1;
    }

    /* Keep every parent pipe copy open until all three children have spawned.
     * Without close-on-spawn, the relay inherits its own write end and cannot
     * see EOF on stdin after the source exits. */
    long children[3];
    children[0] = spawn_pipe_stage("pipe_source", -1, first[1]);
    children[1] = spawn_pipe_stage("pipe_relay", first[0], second[1]);
    children[2] = spawn_pipe_stage("pipe_consume", second[0], -1);
    CHECK(children[0] > 0 && children[1] > 0 && children[2] > 0,
          "three pipeline processes spawned with parent pipe copies open");
    for (int index = 0; index < 2; ++index) {
        int *ends = index ? second : first;
        for (int end = 0; end < 2; ++end) (void)close(ends[end]);
    }
    if (children[0] <= 0 || children[1] <= 0 || children[2] <= 0) {
        for (int index = 0; index < 3; ++index)
            if (children[index] > 0) (void)kill((pid_t)children[index], SIGKILL);
        for (int index = 0; index < 3; ++index) {
            pollikos_wait_t ignored;
            if (children[index] > 0) (void)pollikos_waitpid(children[index], &ignored);
        }
        return 1;
    }
    pollikos_wait_t statuses[3] = {0};
    for (int index = 0; index < 3; ++index) {
        CHECK(pollikos_waitpid(children[index], &statuses[index]) == children[index],
              "pipeline stage waitpid");
        CHECK(statuses[index].kind == POLLIKOS_WAIT_EXITED,
              "pipeline stage exits normally after EOF");
    }
    CHECK(statuses[0].code == 0 && statuses[1].code == 0 && statuses[2].code == 42,
          "three-stage pipeline preserves bytes through EOF");
    return failures ? 1 : 42;
}
static int mode_path(void) {
    unsetenv("PATH");
    long child = pollikos_spawnp("spawn_child_c",
        (const char *const[]){"spawn_child_c", "echo", "first", "second", 0}, test_env);
    CHECK(child > 0, "spawnp finds /bin by default");
    if (child > 0 && wait_exit(child, 42)) return 1;
    setenv("PATH", "/other:/bin", 1);
    child = pollikos_spawnp("spawn_child_c",
        (const char *const[]){"spawn_child_c", "echo", "first", "second", 0}, test_env);
    CHECK(child > 0, "spawnp searches PATH entries in order");
    if (child > 0) return wait_exit(child, 42) ? 1 : 42;
    return failures ? 1 : 42;
}
static int mode_tree(void) {
    long children[4];
    for (unsigned i = 0; i < 4; ++i) {
        children[i] = spawn_child("branch", 0);
        CHECK(children[i] > 0, "tree child spawn");
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (children[i] < 0) continue;
        pollikos_wait_t status;
        long result = pollikos_waitpid(children[i], &status);
        CHECK(result == children[i] && status.kind == POLLIKOS_WAIT_EXITED && status.code == 21,
              "grandchild status returned through the child");
    }
    return failures ? 1 : 42;
}
static int mode_orphan(void) {
    long child = spawn_child("orphan", 0);
    CHECK(child > 0, "orphan child spawn");
    printf("[parent] exiting before child %ld finishes\n", child);
    return failures ? 1 : 42;
}
/* Sustained spawn/wait churn mixing normal exits, hardware faults and
 * SIGKILL-killed blocking children. The default is the documented 5000-cycle
 * bound; an explicit argv[2] overrides it for focused runs. */
static int churn_spawn_wait(const char *path, const char *const arguments[],
                            int expected_code, long cycle, const char *phase) {
    long child = pollikos_spawn(path, arguments, 0);
    if (child <= 0) {
        printf("[parent] churn %s spawn failed cycle=%ld rc=%ld errno=%s\n",
               phase, cycle, child, strerror(errno));
        return 1;
    }
    pollikos_wait_t status;
    long waited = pollikos_waitpid(child, &status);
    if (waited != child) {
        printf("[parent] churn %s wait failed cycle=%ld pid=%ld rc=%ld errno=%s\n",
               phase, cycle, child, waited, strerror(errno));
        return 1;
    }
    if (status.version != POLLIKOS_WAIT_VERSION ||
        status.kind != POLLIKOS_WAIT_EXITED || status.code != expected_code) {
        printf("[parent] churn %s status failed cycle=%ld pid=%ld kind=%u code=%d\n",
               phase, cycle, child, (unsigned)status.kind, (int)status.code);
        return 1;
    }
    return 0;
}
static int churn_tcc_round(long cycle) {
    const char *compile[] = {"tcc", "/home/hello.c", "-o", "/home/hello", 0};
    const char *program[] = {"hello", 0};
    if (churn_spawn_wait("/bin/tcc", compile, 0, cycle, "tcc-compile")) return 1;
    if (churn_spawn_wait("/home/hello", program, 42, cycle, "tcc-output")) return 1;
    return 0;
}
static int mode_churn(int argc, char **argv) {
    long count = 5000;
    if (argc > 2) count = atoi(argv[2]);
    if (count < 1) count = 1;
    long tcc_interval = argc > 3 ? atoi(argv[3]) : 0;
    if (tcc_interval < 0) tcc_interval = 0;
    long exits = 0, faults = 0, kills = 0, tcc_rounds = 0;
    pollikos_wait_t status;
    for (long cycle = 0; cycle < count; ++cycle) {
        long child;
        int kind = (int)(cycle % 5);
        if (kind == 4) {
            const char *arguments[4] = {"spawn_child_c", "sleep", "10000", 0};
            child = pollikos_spawn("/bin/spawn_child_c", arguments, 0);
            if (child <= 0) { CHECK(0, "churn spawn kill"); return 1; }
            if (kill((pid_t)child, SIGKILL) != 0) { CHECK(0, "churn kill"); return 1; }
            if (pollikos_waitpid(child, &status) != child) { CHECK(0, "churn wait kill"); return 1; }
            if (status.kind != POLLIKOS_WAIT_KILLED) { CHECK(0, "churn kill kind"); return 1; }
            ++kills;
        } else if (kind == 3) {
            const char *arguments[3] = {"spawn_child_c", "fault", 0};
            child = pollikos_spawn("/bin/spawn_child_c", arguments, 0);
            if (child <= 0) { CHECK(0, "churn spawn fault"); return 1; }
            if (pollikos_waitpid(child, &status) != child) { CHECK(0, "churn wait fault"); return 1; }
            if (status.kind != POLLIKOS_WAIT_FAULTED) { CHECK(0, "churn fault kind"); return 1; }
            ++faults;
        } else {
            char code[4];
            code[0] = (char)('1' + (cycle % 8));
            code[1] = 0;
            const char *arguments[4] = {"spawn_child_c", "exitcode", code, 0};
            child = pollikos_spawn("/bin/spawn_child_c", arguments, 0);
            if (child <= 0) { CHECK(0, "churn spawn exit"); return 1; }
            if (pollikos_waitpid(child, &status) != child) { CHECK(0, "churn wait exit"); return 1; }
            if (status.kind != POLLIKOS_WAIT_EXITED || status.code != code[0]-'0') {
                CHECK(0, "churn exit status");
                return 1;
            }
            ++exits;
        }
        if (tcc_interval && (cycle+1) % tcc_interval == 0) {
            if (churn_tcc_round(cycle+1)) return 1;
            ++tcc_rounds;
        }
    }
    printf("[parent] churn count=%ld exits=%ld faults=%ld kills=%ld tcc=%ld\n",
           count, exits, faults, kills, tcc_rounds);
    return failures ? 1 : 42;
}
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "basic";
    if (strcmp(mode, "basic") == 0) return mode_basic();
    if (strcmp(mode, "early") == 0) return mode_early();
    if (strcmp(mode, "block") == 0) return mode_block();
    if (strcmp(mode, "multi") == 0) return mode_multi();
    if (strcmp(mode, "fault") == 0) return mode_fault();
    if (strcmp(mode, "killed") == 0) return mode_killed();
    if (strcmp(mode, "errors") == 0) return mode_errors();
    if (strcmp(mode, "env") == 0) return mode_env();
    if (strcmp(mode, "cwd") == 0) return mode_cwd();
    if (strcmp(mode, "fds") == 0) return mode_fds();
    if (strcmp(mode, "cloexec") == 0) return mode_cloexec();
    if (strcmp(mode, "path") == 0) return mode_path();
    if (strcmp(mode, "tree") == 0) return mode_tree();
    if (strcmp(mode, "orphan") == 0) return mode_orphan();
    if (strcmp(mode, "churn") == 0) return mode_churn(argc, argv);
    printf("[C6] unknown mode %s\n", mode);
    return 1;
}
