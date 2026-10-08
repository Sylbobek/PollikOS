/* C6 spawn/wait self-test: SDK C parents spawn children, block in waitpid and
 * collect typed status; kernel checks cover zombie reclamation, orphans, the
 * process limit, failure injection and 100 spawn/wait lifecycles. */
#include "launch.h"
#include "scheduler.h"
#include "file.h"
#include "fs_platform.h"
#include "paging.h"
#include "../../vfs.h"
#include "../../pollikfs.h"
#define check memory_require
#define MUTATION_DIR "/mutation"
static unsigned completions;
static int expected_status;
static uint64_t expected_pids[4];
static void pass(const char *text) { memory_log("[C6] PASS: "); memory_log(text); memory_log("\n"); }
static int ensure_directory(void) {
    vfs_stat_t info;
    if (vfs_stat(MUTATION_DIR, &info) == 0)
        return info.type == VFS_DIR;
    return vfs_mkdir(MUTATION_DIR) == 0;
}
static Process64 *launch_process(const char *path, const char *mode) {
    Process64 *process;
    const char *arguments[2] = {path, mode};
    check(process64_launch_path(path, 2, arguments, 0, 0, &process) == LAUNCH_OK && process,
          "C6 ELF launch by path");
    check(!file64_count(process) && file64_stream_count(process) == 3, "C6 launch ownership");
    check(process64_tick_limit(process->pid, 800), "C6 tick budget");
    return process;
}
static void complete(const Process64 *p) {
    if (p->pid != expected_pids[0] && p->pid != expected_pids[1] &&
        p->pid != expected_pids[2] && p->pid != expected_pids[3])
        return; /* spawned children are the parent's business */
    if (p->state != PROCESS_EXITED || p->exit_status != expected_status) {
        memory_log("[C6] user state="); memory_hex(p->state);
        memory_log(" status="); memory_hex((uint64_t)p->exit_status);
        memory_log(" vector="); memory_hex(p->thread.frame.vector);
        memory_log(" error="); memory_hex(p->thread.frame.error);
        memory_log("\n");
    }
    check(p->state == PROCESS_EXITED && p->exit_status == expected_status, "C6 exit status");
    check(!file64_count(p), "C6 descriptors closed");
    ++completions;
}
static void run_expected(int status, Scheduler64Boundary boundary, Scheduler64Completion completion) {
    expected_status = status;
    check(scheduler64_run(1000, boundary, completion) && !scheduler64_count(), "C6 scheduler completes");
    check(!vfs_debug_handles(), "C6 file handle balance");
}
static void launch_mode(const char *mode, int status) {
    Process64 *process = launch_process("/bin/spawn_parent_c", mode);
    expected_pids[0] = process->pid; expected_pids[1] = expected_pids[2] = expected_pids[3] = 0;
    run_expected(status, 0, complete);
}
static void balance(page_count_t baseline) {
    check(!scheduler64_count() && !vfs_debug_handles() && pmm64_stats().free == baseline &&
          process64_debug_slots() == 0 && process64_debug_zombies() == 0,
          "C6 PMM, handles and process slots return to baseline");
}
static uint64_t read_pid_file(void) {
    vfs_file_t file = {0};
    if (pollikfs_open(MUTATION_DIR "/c6.pid", O_RDONLY, &file) != 0) return 0;
    char buffer[32];
    int count = pollikfs_read(&file, buffer, sizeof(buffer)-1);
    pollikfs_close(&file);
    if (count <= 0) return 0;
    buffer[count] = 0;
    uint64_t pid = 0;
    for (int i = 0; i < count; ++i) {
        if (buffer[i] < '0' || buffer[i] > '9') break;
        pid = pid*10 + (uint64_t)(buffer[i]-'0');
    }
    return pid;
}
static int killed_requested;
static void kill_boundary(void) {
    if (killed_requested) return;
    uint64_t pid = read_pid_file();
    if (pid) {
        check(process64_kill(pid, 9), "C6 kill spinning child");
        killed_requested = 1;
    }
}
static unsigned orphan_completions;
static void orphan_complete(const Process64 *p) {
    check(p->state == PROCESS_EXITED && (p->exit_status == 42 || p->exit_status == 33),
          "C6 orphan test statuses");
    ++orphan_completions;
}
static unsigned killed_completions;
static void killed_complete(const Process64 *p) {
    check(p->state == PROCESS_KILLED && p->exit_status == 124, "C6 tick limit kill status");
    ++killed_completions;
}
static void limit_check(page_count_t baseline) {
    const char *arguments[2] = {"spawn_child_c", "spin"};
    unsigned launched = 0;
    size_t capacity = process64_capacity();
    while (launched < capacity) {
        Process64 *process = 0;
        if (process64_launch_path("/bin/spawn_child_c", 2, arguments, 0, 0, &process) != LAUNCH_OK)
            break;
        check(process64_tick_limit(process->pid, 2), "C6 limit tick budget");
        ++launched;
    }
    check(launched == capacity, "C6 filled every process slot");
    Process64 *extra = 0;
    check(process64_launch_path("/bin/spawn_child_c", 2, arguments, 0, 0, &extra) == LAUNCH_TABLE_FULL &&
          !extra && process64_debug_slots() == capacity, "C6 full table rejects cleanly");
    killed_completions = 0;
    expected_status = 0;
    check(scheduler64_run(capacity*3+32, 0, killed_complete) && !scheduler64_count() &&
          killed_completions == capacity, "C6 full table drains after kills");
    balance(baseline);
    /* A subsequent spawn must work again. */
    launch_mode("basic", 42);
    balance(baseline);
}
static void injection_check(page_count_t baseline) {
    const char *arguments[3] = {"spawn_child_c", "exitcode", "1"};
    int succeeded = 0;
    /* The C7 user stack (33 pages) pushed the spawn allocation count well
     * past the original 48, so sweep further before declaring coverage. */
    for (long long budget = 0; budget < 192 && !succeeded; ++budget) {
        pmm64_fail_after(budget);
        Process64 *process = 0;
        Launch64Result result = process64_launch_path("/bin/spawn_child_c", 3, arguments, 0, 0, &process);
        pmm64_fail_after(-1);
        if (result == LAUNCH_OK) {
            succeeded = 1;
            expected_pids[0] = process->pid;
            expected_pids[1] = expected_pids[2] = expected_pids[3] = 0;
            check(process64_tick_limit(process->pid, 20), "C6 injection success budget");
            run_expected(1, 0, complete);
        } else {
            check(result == LAUNCH_NOMEM && !process, "C6 injected launch failure");
        }
        check(process64_debug_slots() == 0 && process64_debug_zombies() == 0 &&
              pmm64_stats().free == baseline && !vfs_debug_handles(), "C6 spawn rollback balance");
    }
    check(succeeded, "C6 injection prefixes cover the spawn path");
}
/* Sustained spawn/wait churn: normal exits mixed with hardware faults and
 * SIGKILL-killed blocking children. This is the reproducer for the "rare
 * intermittent native spawn failure" recorded in TINYCC_PORT.md. The ordinary
 * suite runs a short prefix; a dedicated image carrying /etc/churn_cycles
 * drives the full documented 5000-cycle bound. */
static unsigned churn_setting(const char *path, unsigned fallback) {
    vfs_file_t file = {0};
    if (pollikfs_open(path, O_RDONLY, &file) != 0) return fallback;
    char buffer[16];
    int count = pollikfs_read(&file, buffer, sizeof(buffer)-1);
    pollikfs_close(&file);
    if (count <= 0) return fallback;
    buffer[count] = 0;
    unsigned value = 0;
    int digits = 0;
    for (int i = 0; i < count && buffer[i] >= '0' && buffer[i] <= '9'; ++i) {
        value = value*10 + (unsigned)(buffer[i]-'0');
        ++digits;
    }
    return digits && value ? value : fallback;
}
static void churn_decimal(char *text, unsigned value) {
    unsigned digits = 1;
    for (unsigned probe = value; probe >= 10; probe /= 10) ++digits;
    text[digits] = 0;
    for (unsigned i = digits; i-- > 0; ) {
        text[i] = (char)('0'+value%10);
        value /= 10;
    }
}
static void churn_check(page_count_t baseline) {
    Process64 *process = 0;
#ifdef POLLIK_TEST_TIMER_HZ
    memory_log("[C6] deterministic timer perturbation hz=137\n");
#endif
    static char cycles_text[16], tcc_interval_text[16];
    unsigned cycles = churn_setting("/etc/churn_cycles", 200);
    unsigned tcc_interval = churn_setting("/etc/churn_tcc_interval", 0);
    churn_decimal(cycles_text, cycles);
    churn_decimal(tcc_interval_text, tcc_interval);
    const char *arguments[4] = {"spawn_parent_c", "churn", cycles_text, tcc_interval_text};
    check(process64_launch_path("/bin/spawn_parent_c", 4, arguments, 0, 0, &process) == LAUNCH_OK && process,
          "C6 churn parent launch");
    expected_pids[0] = process->pid;
    expected_pids[1] = expected_pids[2] = expected_pids[3] = 0;
    check(process64_tick_limit(process->pid, 0), "C6 churn runs without a tick limit");
    expected_status = 42;
    check(scheduler64_run(4000000, 0, complete) && !scheduler64_count(),
          "C6 churn scheduler completes");
    check(!vfs_debug_handles(), "C6 churn VFS handle balance");
    balance(baseline);
}
void c6_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    check(ensure_directory(), "C6 mutation directory");
    (void)vfs_unlink(MUTATION_DIR "/c6.pid");
    balance(baseline);
    churn_check(baseline);
    pass("spawn/wait churn (exit/fault/kill mix) returns to baseline");
    launch_mode("basic", 42);
    pass("C parent spawns, waits and inspects child exit code 42");
    balance(baseline);
    launch_mode("early", 42);
    pass("child that exits before wait is collected later");
    launch_mode("block", 42);
    pass("wait blocks without busy-spin until child exit");
    launch_mode("multi", 42);
    pass("multiple children associate pid and status correctly");
    launch_mode("fault", 42);
    pass("faulting child returns fault termination and the parent survives");
    killed_requested = 0;
    expected_pids[0] = launch_process("/bin/spawn_parent_c", "killed")->pid; expected_pids[1] = expected_pids[2] = expected_pids[3] = 0;
    run_expected(42, kill_boundary, complete);
    check(killed_requested, "C6 kill request delivered");
    (void)vfs_unlink(MUTATION_DIR "/c6.pid");
    balance(baseline);
    pass("killed child returns kill termination");
    launch_mode("errors", 42);
    pass("invalid spawn and wait requests fail safely");
    launch_mode("env", 42);
    pass("environment inherited by spawn and mutated locally");
    launch_mode("cwd", 42);
    pass("cwd inherited and independent");
    launch_mode("fds", 42);
    pass("descriptor inheritance shares offset and refcounts");
    launch_mode("cloexec", 42);
    balance(baseline);
    pass("close-on-spawn descriptors and three-stage pipe EOF");
    launch_mode("path", 42);
    pass("PATH lookup in libc spawnp");
    launch_mode("tree", 42);
    pass("bounded parent/child tree ownership");
    orphan_completions = 0;
    launch_process("/bin/spawn_parent_c", "orphan");
    check(scheduler64_run(600, 0, orphan_complete) && !scheduler64_count() &&
          orphan_completions == 2, "C6 orphan child completes after parent exit");
    balance(baseline);
    pass("orphan children auto-reap when the parent exits");
    limit_check(baseline);
    pass("process table exhaustion fails cleanly and recovers");
    injection_check(baseline);
    pass("injected spawn failures roll back completely");
    unsigned before = completions;
    for (unsigned batch = 0; batch < 25; ++batch) {
        for (unsigned index = 0; index < 4; ++index)
            expected_pids[index] = launch_process("/bin/spawn_parent_c", "basic")->pid;
        run_expected(42, 0, complete);
        balance(baseline);
    }
    check(completions-before == 100, "C6 100 spawn/wait lifecycles");
    memory_log("[C6] balance pmm="); memory_hex(baseline);
    memory_log(" handles="); memory_hex(vfs_debug_handles());
    memory_log(" slots="); memory_hex(process64_debug_slots());
    memory_log(" zombies="); memory_hex(process64_debug_zombies()); memory_log("\n");
    pass("100 spawn/wait lifecycles return PMM exactly to baseline");
}
