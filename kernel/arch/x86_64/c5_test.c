/* C5 filesystem mutation self-test: SDK C programs create, write, append,
 * truncate, rename and remove files; kernel checks cover failure injection,
 * disk-resource balance, reboot persistence and disk-full safety. */
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
static int reboot_verified;
static void pass(const char *text) { memory_log("[C5] PASS: "); memory_log(text); memory_log("\n"); }
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
          "C5 ELF launch by path");
    check(!file64_count(process) && file64_stream_count(process) == 3, "C5 launch ownership");
    check(process64_tick_limit(process->pid, 800), "C5 tick budget");
    return process;
}
static void complete(const Process64 *p) {
    if (p->state != PROCESS_EXITED || p->exit_status != expected_status) {
        memory_log("[C5] user state="); memory_hex(p->state);
        memory_log(" status="); memory_hex((uint64_t)p->exit_status);
        memory_log(" vector="); memory_hex(p->thread.frame.vector);
        memory_log(" error="); memory_hex(p->thread.frame.error);
        memory_log("\n");
    }
    check(p->state == PROCESS_EXITED && p->exit_status == expected_status, "C5 exit status");
    check(!file64_count(p), "C5 descriptors closed");
    ++completions;
}
static void run_expected(int status, Scheduler64Boundary boundary, Scheduler64Completion completion) {
    expected_status = status;
    check(scheduler64_run(900, boundary, completion) && !scheduler64_count(), "C5 scheduler completes");
    check(!vfs_debug_handles(), "C5 file handle balance");
}
static void run_mode(const char *mode, int status) {
    launch_process("/bin/mutation_c", mode);
    run_expected(status, 0, complete);
}
static void launch_expected(const char *path, const char *mode, int status) {
    launch_process(path, mode);
    run_expected(status, 0, complete);
}
static void fs_balance(u32 blocks, u32 inodes, page_count_t pmm) {
    check(!scheduler64_count() && !vfs_debug_handles() && pmm64_stats().free == pmm,
          "C5 process and VFS balance");
    check(pollikfs_free_blocks() == blocks && pollikfs_free_inodes() == inodes,
          "C5 PollikFS block and inode balance");
}
/* Reboot persistence: the marker is written at the end of every boot; a later
 * boot on the same image must read identical contents. */
static const char reboot_content[] = "persisted-across-reboot\n";
static void reboot_probe(void) {
    vfs_stat_t info;
    if (vfs_stat(MUTATION_DIR "/reboot.txt", &info) != 0)
        return;
    if (info.size != sizeof(reboot_content)-1)
        return;
    vfs_file_t file = {0};
    if (pollikfs_open(MUTATION_DIR "/reboot.txt", O_RDONLY, &file) != 0)
        return;
    char buffer[sizeof(reboot_content)];
    int count = pollikfs_read(&file, buffer, sizeof(buffer));
    pollikfs_close(&file);
    if (count != (int)(sizeof(reboot_content)-1))
        return;
    for (unsigned i = 0; i < sizeof(reboot_content)-1; ++i)
        if (buffer[i] != reboot_content[i]) return;
    reboot_verified = 1;
}
static void reboot_write(void) {
    vfs_file_t file = {0};
    check(pollikfs_open(MUTATION_DIR "/reboot.txt", O_WRONLY|O_CREAT|O_TRUNC, &file) == 0,
          "C5 reboot marker create");
    check(pollikfs_write(&file, reboot_content, sizeof(reboot_content)-1) ==
          (int)(sizeof(reboot_content)-1), "C5 reboot marker write");
    pollikfs_close(&file);
}
/* Allocation-failure injection across create, write and directory growth. */
static void entry_path(char *path, const char *directory, unsigned index) {
    unsigned length = 0;
    while (directory[length]) { path[length] = directory[length]; ++length; }
    path[length] = 'f';
    path[length+1] = (char)('a' + index/10);
    path[length+2] = (char)('a' + index%10);
    path[length+3] = 0;
}
static void injection_checks(u32 blocks, u32 inodes, page_count_t pmm) {
    u32 before_blocks = pollikfs_free_blocks();
    u32 before_inodes = pollikfs_free_inodes();
    int succeeded = 0;
    for (long long budget = 0; budget < 40 && !succeeded; ++budget) {
        pollikfs_fail_after(budget);
        vfs_file_t file = {0};
        int created = pollikfs_open(MUTATION_DIR "/inj.bin", O_WRONLY|O_CREAT|O_TRUNC, &file);
        if (created == 0) {
            char payload[3000];
            for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = (char)(i*3u+1u);
            if (pollikfs_write(&file, payload, sizeof(payload)) == (int)sizeof(payload))
                succeeded = 1;
            pollikfs_close(&file);
        }
        pollikfs_fail_after(-1);
        (void)pollikfs_unlink(MUTATION_DIR "/inj.bin");
        pollikfs_init();
        check(pollikfs_mounted(), "C5 remount after injected failure");
        check(pollikfs_free_blocks() == before_blocks && pollikfs_free_inodes() == before_inodes,
              "C5 injected failure leaks no blocks or inodes");
    }
    check(succeeded, "C5 injection budgets cover create and write prefixes");
    /* Directory growth needs a 17th entry: fill 16 then fail the new block. */
    check(pollikfs_mkdir(MUTATION_DIR "/injdir") == 0, "C5 injection directory create");
    char path[64];
    for (unsigned index = 0; index < 16; ++index) {
        pollikfs_fail_after(-1);
        entry_path(path, MUTATION_DIR "/injdir/", index);
        vfs_file_t file = {0};
        check(pollikfs_open(path, O_WRONLY|O_CREAT|O_TRUNC, &file) == 0,
              "C5 fill directory entry");
        pollikfs_close(&file);
    }
    int growth_failed = 0, growth_succeeded = 0;
    for (long long budget = 0; budget < 6; ++budget) {
        pollikfs_fail_after(budget);
        vfs_file_t file = {0};
        int created = pollikfs_open(MUTATION_DIR "/injdir/growth", O_WRONLY|O_CREAT|O_TRUNC, &file);
        pollikfs_fail_after(-1);
        if (created == 0) {
            pollikfs_close(&file);
            growth_succeeded = 1;
            (void)pollikfs_unlink(MUTATION_DIR "/injdir/growth");
        } else {
            growth_failed = 1;
        }
        pollikfs_init();
        check(pollikfs_mounted(), "C5 remount after directory growth failure");
    }
    check(growth_failed && growth_succeeded, "C5 directory growth failure and success covered");
    for (unsigned index = 0; index < 16; ++index) {
        entry_path(path, MUTATION_DIR "/injdir/", index);
        (void)pollikfs_unlink(path);
    }
    check(pollikfs_rmdir(MUTATION_DIR "/injdir") == 0, "C5 injection directory removal");
    check(pollikfs_free_blocks() == before_blocks && pollikfs_free_inodes() == before_inodes,
          "C5 injection cleanup balance");
    fs_balance(blocks, inodes, pmm);
}
/* A replace mutates existing directory records and must not depend on an
 * allocation succeeding. Exercise the allocation-failure budgets with source
 * and target in separate parents, then remount and verify exact accounting. */
static void replace_injection_checks(u32 blocks, u32 inodes, page_count_t pmm) {
    static const char directory[] = MUTATION_DIR "/replace-dir";
    static const char source[] = MUTATION_DIR "/replace-dir/source";
    static const char target[] = MUTATION_DIR "/replace-target";
    for (long long budget = 0; budget < 3; ++budget) {
        (void)pollikfs_unlink(source);
        (void)pollikfs_unlink(target);
        (void)pollikfs_rmdir(directory);
        check(pollikfs_mkdir(directory) == 0, "C5 rename_replace source directory");
        vfs_file_t file = {0};
        check(pollikfs_open(source, O_WRONLY|O_CREAT|O_TRUNC, &file) == 0,
              "C5 rename_replace source create");
        check(pollikfs_write(&file, "new!", 4) == 4, "C5 rename_replace source write");
        pollikfs_close(&file);
        check(pollikfs_open(target, O_WRONLY|O_CREAT|O_TRUNC, &file) == 0,
              "C5 rename_replace target create");
        check(pollikfs_write(&file, "old!", 4) == 4, "C5 rename_replace target write");
        pollikfs_close(&file);
        u32 before_blocks = pollikfs_free_blocks();
        u32 before_inodes = pollikfs_free_inodes();

        if (budget == 0) {
            fs64_io_fail_after(0);
            int io_failed = pollikfs_rename_replace(source, target);
            fs64_io_fail_after(-1);
            check(io_failed < 0, "C5 rename_replace reports injected pre-commit I/O failure");
            pollikfs_init();
            vfs_stat_t original_source, original_target;
            check(pollikfs_mounted() && vfs_stat(source, &original_source) == 0 &&
                  vfs_stat(target, &original_target) == 0 &&
                  original_source.size == 4 && original_target.size == 4 &&
                  pollikfs_free_blocks() == before_blocks &&
                  pollikfs_free_inodes() == before_inodes,
                  "C5 rename_replace I/O failure preserves both names and accounting");
            vfs_file_t verify = {0};
            char bytes[4];
            int source_ok = pollikfs_open(source, O_RDONLY, &verify) == 0 &&
                pollikfs_read(&verify, bytes, sizeof(bytes)) == 4 &&
                bytes[0] == 'n' && bytes[1] == 'e' && bytes[2] == 'w' && bytes[3] == '!';
            pollikfs_close(&verify);
            int target_ok = pollikfs_open(target, O_RDONLY, &verify) == 0 &&
                pollikfs_read(&verify, bytes, sizeof(bytes)) == 4 &&
                bytes[0] == 'o' && bytes[1] == 'l' && bytes[2] == 'd' && bytes[3] == '!';
            pollikfs_close(&verify);
            check(source_ok && target_ok,
                  "C5 rename_replace I/O failure preserves both file payloads");
        }

        pollikfs_fail_after(budget);
        int renamed = pollikfs_rename_replace(source, target);
        pollikfs_fail_after(-1);
        check(renamed == 0, "C5 rename_replace succeeds under allocation failure budget");
        pollikfs_init();
        check(pollikfs_mounted(), "C5 rename_replace remount after failure injection");
        check(pollikfs_free_blocks() == before_blocks+1 &&
              pollikfs_free_inodes() == before_inodes+1,
              "C5 rename_replace reclaims replaced file blocks and inode");
        vfs_stat_t info;
        check(vfs_stat(source, &info) < 0 && vfs_stat(target, &info) == 0 && info.size == 4,
              "C5 rename_replace commits only destination name");
        check(pollikfs_open(target, O_RDONLY, &file) == 0,
              "C5 rename_replace target reopen");
        char contents[4];
        check(pollikfs_read(&file, contents, sizeof(contents)) == 4 &&
              contents[0] == 'n' && contents[1] == 'e' &&
              contents[2] == 'w' && contents[3] == '!',
              "C5 rename_replace contents survive remount");
        pollikfs_close(&file);
        check(pollikfs_unlink(target) == 0 && pollikfs_rmdir(directory) == 0,
              "C5 rename_replace cleanup");
        pollikfs_init();
        check(pollikfs_mounted() && pollikfs_free_blocks() == blocks &&
              pollikfs_free_inodes() == inodes,
              "C5 rename_replace returns exact block/inode baseline");
    }
    fs_balance(blocks, inodes, pmm);
}
void c5_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    vfs_stat_t info;
    check(ensure_directory(), "C5 mutation directory");
    if (vfs_stat("/etc/diskfull", &info) == 0) {
        /* Dedicated full image: only the ENOSPC safety path is meaningful. */
        u32 blocks = pollikfs_free_blocks(), inodes = pollikfs_free_inodes();
        launch_expected("/bin/mutation_c", "diskfull", 42);
        pollikfs_init();
        check(pollikfs_mounted() && vfs_stat("/etc/diskfull", &info) == 0,
              "C5 full filesystem remounts after ENOSPC");
        fs_balance(blocks, inodes, baseline);
        memory_log("[C5] balance pmm="); memory_hex(baseline);
        memory_log(" blocks="); memory_hex(pollikfs_free_blocks());
        memory_log(" inodes="); memory_hex(pollikfs_free_inodes()); memory_log(" handles=0\n");
        pass("disk full returns ENOSPC safely and keeps PollikFS mountable");
        return;
    }
    reboot_probe();
    u32 blocks = pollikfs_free_blocks();
    u32 inodes = pollikfs_free_inodes();
    completions = 0;
    run_mode("basic", 42);
    pass("regular-file create/write/read/verify through the SDK");
    fs_balance(blocks, inodes, baseline);
    run_mode("multi", 42);
    pass("multi-write offset advance, block boundaries and exact contents");
    run_mode("large", 42);
    pass("large file crosses direct/indirect blocks and reads back exactly");
    fs_balance(blocks, inodes, baseline);
    run_mode("append", 42);
    pass("append writes at current EOF (ABC + DEF)");
    run_mode("truncate", 42);
    pass("O_TRUNC releases blocks and resets size");
    fs_balance(blocks, inodes, baseline);
    run_mode("dirs", 42);
    pass("mkdir/rename/read/unlink/rmdir mutation sequence");
    run_mode("clock", 42);
    pass("RTC realtime syscall, time and gettimeofday agree");
    run_mode("relative", 42);
    pass("relative create/rename/unlink through the shared resolver");
    fs_balance(blocks, inodes, baseline);
    run_mode("unlinkreuse", 42);
    pass("unlink-while-open never observes or modifies a reused inode");
    fs_balance(blocks, inodes, baseline);
    run_mode("flags", 42);
    pass("bad pointers, bad flags and access modes return stable errors");
    fs_balance(blocks, inodes, baseline);
    completions = 0;
    launch_process("/bin/mutation_c", "peer");
    launch_process("/bin/mutation_c", "peer");
    run_expected(42, 0, complete);
    check(completions == 2, "C5 concurrent peers complete");
    fs_balance(blocks, inodes, baseline);
    pass("two concurrent writers keep independent descriptors and data");
    injection_checks(blocks, inodes, baseline);
    fs_balance(blocks, inodes, baseline);
    pass("injected allocation failures leave PollikFS mountable and balanced");
    replace_injection_checks(blocks, inodes, baseline);
    pass("rename_replace survives allocation budgets and remounts with exact accounting");
    unsigned before = completions;
    for (unsigned batch = 0; batch < 25; ++batch) {
        for (unsigned index = 0; index < 4; ++index)
            launch_process("/bin/mutation_c", "lifecycle");
        run_expected(42, 0, complete);
        fs_balance(blocks, inodes, baseline);
    }
    check(completions-before == 100, "C5 100 mutation lifecycles");
    reboot_write();
    if (reboot_verified)
        pass("reboot persistence: committed files survive a fresh mount");
    memory_log("[C5] balance pmm="); memory_hex(baseline);
    memory_log(" blocks="); memory_hex(pollikfs_free_blocks());
    memory_log(" inodes="); memory_hex(pollikfs_free_inodes()); memory_log(" handles=0\n");
    pass("100 mutation lifecycles return PMM, VFS, block and inode baselines");
}
