#include "launch.h"
#include "scheduler.h"
#include "fs_platform.h"
#include "../../vfs.h"
#include "../../pollikfs.h"
#include "test_memory.h"
#define check memory_require
uint8_t *hello_elf_start, *schedule_elf_start;
size_t hello_elf_size, schedule_elf_size;
static const char *args[] = {"hello.elf", "first", "second"};
static const char *env[] = {"TEST=pollikos"};
static unsigned completed, exited, faulted, killed;
static void pass(const char *s) { memory_log("[PATH64] PASS: "); memory_log(s); memory_log("\n"); }
static void fixture(const char *path, uint8_t *buffer, size_t *size) {
    vfs_stat_t st;
    check(vfs_stat(path, &st) == 0 && st.size <= 65536, "disk regression fixture stat");
    int fd = vfs_open(path, O_RDONLY);
    check(fd >= 0, "disk regression fixture open");
    *size = st.size;
    size_t received = 0;
    while (received < *size) {
        int n = vfs_read(fd, buffer+received, (u32)(*size-received));
        check(n > 0 && (size_t)n <= *size-received, "disk regression fixture exact read");
        received += (unsigned)n;
    }
    check(vfs_close(fd) == 0, "disk regression fixture close");
}
void disk64_load_regression_fixtures(void) {
    hello_elf_start = kernel_test_buffer_alloc(65536);
    schedule_elf_start = kernel_test_buffer_alloc(65536);
    check(hello_elf_start && schedule_elf_start, "test ELF buffers allocate after boot");
    fixture("/bin/hello", hello_elf_start, &hello_elf_size);
    fixture("/bin/spin", schedule_elf_start, &schedule_elf_size);
    check(!vfs_debug_handles(), "regression fixture handle balance");
}
void disk64_release_regression_fixtures(void) {
    kernel_test_buffer_free(schedule_elf_start);
    kernel_test_buffer_free(hello_elf_start);
    schedule_elf_start = hello_elf_start = 0;
    schedule_elf_size = hello_elf_size = 0;
}
static void complete(const Process64 *p) {
    check(!vfs_debug_handles(), "file closed before process executes");
    check(p->name[0] && p->name[55] == 0, "owned bounded process basename");
    if (p->state == PROCESS_EXITED) { check(p->exit_status == 42, "path exit/argv/envp status"); ++exited; }
    else if (p->state == PROCESS_FAULTED) {
        check(p->thread.frame.vector == 14 && p->thread.frame.error == 7, "path text fault containment"); ++faulted;
    } else {
        check(p->state == PROCESS_KILLED && p->exit_status == 124 && p->thread.ticks == 2,
              "path spin timer kill"); ++killed;
    }
    ++completed;
}
static Process64 *launch(const char *path, int arguments) {
    Process64 *p;
    check(process64_launch_path(path, arguments ? 3 : 0, arguments ? args : 0,
        arguments ? 1 : 0, arguments ? env : 0, &p) == LAUNCH_OK && p, "path launch succeeds");
    check(!vfs_debug_handles(), "path launch releases file handle");
    return p;
}
static void drain(void) {
    int ran = scheduler64_run(process64_capacity()*3+32, 0, complete);
    size_t live = scheduler64_count();
    if (!ran || live) {
        Scheduler64Stats stats = scheduler64_stats();
        memory_log("[PATH64] drain diagnostic live="); memory_hex(live);
        memory_log(" slots="); memory_hex(process64_debug_slots());
        memory_log(" zombies="); memory_hex(process64_debug_zombies());
        memory_log(" ticks="); memory_hex(stats.ticks);
        memory_log(" dispatches="); memory_hex(stats.dispatches);
        memory_log(" reaped="); memory_hex(stats.reaped); memory_log("\n");
    }
    check(ran && !live, "path scheduler drain");
}
static void failure(const char *path, Launch64Result expected, page_count_t baseline) {
    Process64 *p = (Process64 *)1;
    Launch64Result error = process64_launch_path(path, 3, args, 1, env, &p);
    if (error != expected) { memory_log("[PATH64] unexpected "); memory_log(path); memory_log(": "); memory_log(launch64_error_name(error)); memory_log("\n"); }
    check(error == expected && !p && !vfs_debug_handles() && !scheduler64_count() &&
          pmm64_stats().free == baseline, "path failure ownership and error");
}
void path64_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    check(pollikfs_mounted() && !vfs_debug_handles() && !scheduler64_count(), "path test baseline");
    vfs_stat_t st;
    check(vfs_stat("/bin", &st) == 0 && st.type == VFS_DIR, "canonical bin directory");
    const char *paths[] = {"/bin/hello", "/bin/argvtest", "/bin/spin", "/bin/faulttest"};
    for (unsigned i = 0; i < 4; ++i) {
        check(vfs_stat(paths[i], &st) == 0 && st.type == VFS_FILE && st.size >= 64, "disk executable stat");
        memory_log("[PATH64] stat "); memory_log(paths[i]); memory_log(" bytes="); memory_hex(st.size); memory_log("\n");
    }
    int fd = vfs_open("/bin", O_RDONLY);
    vfs_dirent_t entry;
    unsigned entries = 0;
    int n;
    check(fd >= 0, "directory open");
    while ((n = vfs_readdir(fd, &entry)) > 0) { check(++entries <= 64, "bounded directory enumeration"); }
    if (n != 0 || entries != 55) {
        memory_log("[PATH64] directory count="); memory_hex(entries);
        memory_log(" result="); memory_hex((uint64_t)n); memory_log("\n");
    }
    /* Calculator was appended to the fixture; keep an exact count rather
     * than a lower bound. Both normal and SelfTest images contain 55 files. */
    check(n == 0 && entries == 55, "disk files visible through VFS");
    check(vfs_close(fd) == 0, "directory close");
    fd = vfs_open("/bin/hello", O_RDONLY);
    uint8_t header[4];
    check(fd >= 0 && vfs_seek(fd, 1, SEEK_SET) == 1 && vfs_read(fd, header, 3) == 3 &&
        header[0] == 'E' && header[1] == 'L' && header[2] == 'F' && vfs_close(fd) == 0, "VFS seek/read/close");
    pass("PollikFS v2 mount, bin enumeration, stat and seek/read/close");

    failure("/bin/missing", LAUNCH_NOT_FOUND, baseline);
    failure("/bin", LAUNCH_NOT_FILE, baseline);
    failure("/bin/empty", LAUNCH_BAD_SIZE, baseline);
    failure("/bin/tiny", LAUNCH_BAD_SIZE, baseline);
    failure("/bin/truncated", LAUNCH_INVALID_ELF, baseline);
    failure("/bin/invalid", LAUNCH_INVALID_ELF, baseline);
    failure("/bin/dynamic", LAUNCH_UNSUPPORTED_ELF, baseline);
    failure("/bin/large", LAUNCH_TOO_LARGE, baseline); /* above the raised loader cap */
    failure("relative", LAUNCH_BAD_REQUEST, baseline);
    failure("/bin/../hello", LAUNCH_BAD_REQUEST, baseline);
    pass("missing/directory/empty/truncated/large/invalid/dynamic rejection without leaks");

    launch64_read_inject(113, -1);
    launch("/bin/hello", 1); drain();
    launch64_read_inject(113, 1000);
    failure("/bin/hello", LAUNCH_IO, baseline);
    launch64_read_inject(0, -1);
    int succeeded = 0;
    for (int64_t sectors = 0; sectors < 128; ++sectors) {
        fs64_io_fail_after(sectors);
        Process64 *p;
        Launch64Result result = process64_launch_path("/bin/hello", 3, args, 1, env, &p);
        fs64_io_fail_after(-1);
        if (result == LAUNCH_OK) { succeeded = 1; drain(); }
        else check(result == LAUNCH_IO && !p, "ATA failure during lookup/open/read");
        check(!vfs_debug_handles() && !scheduler64_count() && pmm64_stats().free == baseline,
              "I/O failure rollback including indirect ELF blocks");
        if (succeeded) break;
    }
    check(succeeded, "ATA failure prefixes through full read");
    pass("partial reads, early EOF and ATA I/O failure rollback");

    succeeded = 0;
    for (int64_t budget = 0; budget < 128; ++budget) {
        pmm64_fail_after(budget);
        Process64 *p;
        Launch64Result result = process64_launch_path("/bin/hello", 3, args, 1, env, &p);
        pmm64_fail_after(-1);
        if (result == LAUNCH_OK) { succeeded = 1; drain(); }
        else check(result == LAUNCH_NOMEM && !p, "buffer/process/ELF allocation failure");
        check(!vfs_debug_handles() && !scheduler64_count() && pmm64_stats().free == baseline,
              "launch allocation prefix rollback");
        if (succeeded) break;
    }
    check(succeeded, "all allocation prefixes covered");
    scheduler64_fail_submit(1);
    failure("/bin/hello", LAUNCH_QUEUE, baseline);
    scheduler64_fail_submit(0);
    size_t capacity = process64_capacity();
    for (size_t i = 0; i < capacity; ++i) launch("/bin/hello", 1);
    page_count_t full = pmm64_stats().free;
    Process64 *extra;
    check(process64_launch_path("/bin/hello", 3, args, 1, env, &extra) == LAUNCH_TABLE_FULL && !extra &&
          scheduler64_count() == capacity && pmm64_stats().free == full && !vfs_debug_handles(),
          "full process table clean failure");
    drain();
    pass("buffer/process allocation, full slots and scheduler insertion rollback");

    char path[] = "/bin/argvtest", arg0[] = "hello.elf", arg1[] = "first", arg2[] = "second", environment[] = "TEST=pollikos";
    const char *local_args[] = {arg0, arg1, arg2};
    const char *local_env[] = {environment};
    Process64 *p;
    check(process64_launch_path(path, 3, local_args, 1, local_env, &p) == LAUNCH_OK, "owned request strings");
    path[5] = 'X'; arg0[0] = 'X'; arg1[0] = 'X'; arg2[0] = 'X'; environment[0] = 'X';
    check(p->name[0] == 'a' && p->name[7] == 't' && !p->name[8], "name copied before caller mutation");
    drain();
    pass("startup ABI v1 argv/envp and diagnostic path ownership");

    unsigned before = completed, before_exit = exited, before_fault = faulted, before_kill = killed;
    for (unsigned cycle = 0; cycle < 25; ++cycle) {
        /* Spin is first in the queue: peers only run after timer preemption. */
        p = launch("/bin/spin", 0);
        check(process64_tick_limit(p->pid, 2), "file-loaded spin time limit");
        launch("/bin/hello", 1); launch("/bin/argvtest", 1); launch("/bin/faulttest", 0);
        drain();
        check(!vfs_debug_handles() && pmm64_stats().free == baseline, "path lifecycle balance each batch");
    }
    check(completed-before == 100 && exited-before_exit == 50 && faulted-before_fault == 25 && killed-before_kill == 25,
          "100 file-loaded exit/fault/kill lifecycles");
    memory_log("[PATH64] balance before="); memory_hex(baseline);
    memory_log(" after="); memory_hex(pmm64_stats().free);
    memory_log(" handles="); memory_hex(vfs_debug_handles()); memory_log("\n");
    pass("100 path launches: preemption, exit/fault/kill, PMM and handles balanced");
}
