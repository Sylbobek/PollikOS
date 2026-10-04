/* C7 compiler-readiness self-test: FILE streams, libc completion, allocator
 * stress, compiler-like multi-process orchestration and raised limits. */
#include "launch.h"
#include "scheduler.h"
#include "file.h"
#include "fs_platform.h"
#include "paging.h"
#include "../../vfs.h"
#include "../../pollikfs.h"
#define check memory_require
static unsigned completions;
static int expected_status;
static uint64_t expected_pids[4];
static void pass(const char *text) { memory_log("[C7] PASS: "); memory_log(text); memory_log("\n"); }
static Process64 *launch_process(const char *path, const char *mode) {
    Process64 *process;
    const char *arguments[2] = {path, mode};
    check(process64_launch_path(path, 2, arguments, 0, 0, &process) == LAUNCH_OK && process,
          "C7 ELF launch by path");
    check(!file64_count(process) && file64_stream_count(process) == 3, "C7 launch ownership");
    check(process64_tick_limit(process->pid, 900), "C7 tick budget");
    return process;
}
static void complete(const Process64 *p) {
    if (p->pid != expected_pids[0] && p->pid != expected_pids[1] &&
        p->pid != expected_pids[2] && p->pid != expected_pids[3])
        return; /* spawned helpers belong to the parent tool */
    if (p->state != PROCESS_EXITED || p->exit_status != expected_status) {
        memory_log("[C7] user state="); memory_hex(p->state);
        memory_log(" status="); memory_hex((uint64_t)p->exit_status);
        memory_log(" vector="); memory_hex(p->frame.vector);
        memory_log(" error="); memory_hex(p->frame.error);
        memory_log("\n");
    }
    check(p->state == PROCESS_EXITED && p->exit_status == expected_status, "C7 exit status");
    check(!file64_count(p), "C7 descriptors closed");
    ++completions;
}
static void run_expected(int status) {
    expected_status = status;
    check(scheduler64_run(1200, 0, complete) && !scheduler64_count(), "C7 scheduler completes");
    check(!vfs_debug_handles(), "C7 file handle balance");
}
static void launch_mode(const char *path, const char *mode, int status) {
    Process64 *process = launch_process(path, mode);
    expected_pids[0] = process->pid;
    expected_pids[1] = expected_pids[2] = expected_pids[3] = 0;
    run_expected(status);
}
static void ensure_directory(const char *path) {
    vfs_stat_t info;
    if (vfs_stat(path, &info) == 0) {
        check(info.type == VFS_DIR, "C7 directory type");
        return;
    }
    check(vfs_mkdir(path) == 0, "C7 directory create");
}
static void balance(page_count_t pmm, u32 blocks, u32 inodes) {
    check(!scheduler64_count() && !vfs_debug_handles() && pmm64_stats().free == pmm &&
          process64_debug_slots() == 0 && process64_debug_zombies() == 0,
          "C7 process and PMM balance");
    check(pollikfs_free_blocks() == blocks && pollikfs_free_inodes() == inodes,
          "C7 filesystem balance");
}
void c7_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    ensure_directory("/tmp");
    ensure_directory("/tmp/c7");
    u32 blocks = pollikfs_free_blocks();
    u32 inodes = pollikfs_free_inodes();
    balance(baseline, blocks, inodes);
    launch_mode("/bin/stdio_c", "basic", 42);
    launch_mode("/bin/stdio_c", "modes", 42);
    launch_mode("/bin/stdio_c", "seek", 42);
    balance(baseline, blocks, inodes);
    pass("FILE fopen/fclose/fread/fwrite/fseek/ftell/fgets/fputs/ungetc");
    pass("fopen modes r/w/a/r+/w+/a+ and buffered flush");
    pass("multi-block FILE seek and overwrite integrity");
    launch_mode("/bin/stdio_c", "errors", 42);
    launch_mode("/bin/stdio_c", "dir", 42);
    balance(baseline, blocks, inodes);
    pass("FILE EOF/error state, remove and access");
    launch_mode("/bin/stdio_c", "deep", 42);
    pass("deep stack frames inside the 256 KiB user stack");
    launch_mode("/bin/stdio_c", "fds", 42);
    pass("128-descriptor limit and cleanup");
    launch_mode("/bin/libc_c", "basic", 42);
    pass("ctype, string, conversion, qsort/bsearch and strerror");
    launch_mode("/bin/alloc_stress_c", "symbols", 42);
    launch_mode("/bin/alloc_stress_c", "growth", 42);
    balance(baseline, blocks, inodes);
    pass("allocator symbol-table and growth stress");
    launch_mode("/bin/tool_driver_c", "chain", 42);
    pass("three-stage tool chain via spawn/wait and files");
    launch_mode("/bin/tool_driver_c", "parallel", 42);
    pass("eight concurrent compiler-like helpers");
    launch_mode("/bin/tool_driver_c", "temp", 42);
    pass("temporary files isolate concurrent processes and clean up");
    launch_mode("/bin/tool_driver_c", "args", 42);
    pass("large bounded argv arrays reach the helper");
    launch_mode("/bin/big_elf_c", "run", 42);
    pass("300 KiB ELF loads under the raised executable cap");
    (void)vfs_unlink("/tmp/c7/.keep");
    balance(baseline, blocks, inodes);
    memory_log("[C7] balance pmm="); memory_hex(baseline);
    memory_log(" blocks="); memory_hex(pollikfs_free_blocks());
    memory_log(" inodes="); memory_hex(pollikfs_free_inodes());
    memory_log(" handles="); memory_hex(vfs_debug_handles());
    memory_log(" slots="); memory_hex(process64_debug_slots()); memory_log("\n");
    pass("compiler-readiness resources return to baseline");
}
