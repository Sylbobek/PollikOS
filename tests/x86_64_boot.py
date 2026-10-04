"""Run actual x86_64 guest tests in QEMU TCG with generated read-only-use data fixtures.

First run build-x86_64.ps1 and build-x86_64.ps1 -SelfTest.
Host Python only supervises the VM and checks its serial diagnostics.
"""
from pathlib import Path
import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "x86_64"


def fresh_data(variant, suffix=""):
    """Copy the pristine disposable data fixture to a per-run image. C5 tests
    intentionally mutate the filesystem, so the pristine fixture stays intact
    and each boot starts from the same state."""
    source = BUILD / variant / "PollikData-test.img"
    target = BUILD / variant / f"PollikData-run{suffix}.img"
    shutil.copyfile(source, target)
    return target


def boot(variant, cpu, ram, markers, data_image="default", suffix="", mutable_data=False,
         deadline=180):
    directory = BUILD / variant
    elf = (directory / "kernel.elf").read_bytes()
    assert elf[:6] == b"\x7fELF\x02\x01", "expected little-endian ELF64"
    assert struct.unpack_from("<H", elf, 18)[0] == 62, "expected EM_X86_64"
    assert struct.unpack_from("<Q", elf, 24)[0] == 0x100000, "BIOS entry contract"
    # Canonical name plus bounded fallbacks written when a host filter holds
    # the previous file; always boot the newest complete image.
    images = sorted(directory.glob("PollikOS-x86_64*.img"),
                    key=lambda path: path.stat().st_mtime, reverse=True)
    assert images, f"no x86_64 boot image in {directory}"
    image = images[0]
    # Logs survive successful and failed runs, while the image is snapshot-only.
    label = f"{variant}-{cpu.replace(',', '_')}-{ram}{suffix}"
    data_image = directory / "PollikData-test.img" if data_image == "default" else data_image
    data_hash = None
    if data_image and not mutable_data:
        data_hash = hashlib.sha256(Path(data_image).read_bytes()).hexdigest()
    data_args = ["-drive", f"file={data_image},format=raw,if=ide,index=1"] if data_image else []
    log = BUILD / f"{label}.log"
    with tempfile.TemporaryDirectory(prefix="pollikos-x64-") as temporary:
        serial = Path(temporary) / "serial.log"
        process = subprocess.Popen([
            "qemu-system-x86_64", "-accel", "tcg", "-machine", "pc",
            "-cpu", cpu, "-m", str(ram), "-vga", "std", "-nic", "none",
            "-display", "none", "-monitor", "none", "-serial", f"file:{serial}",
            "-no-reboot", "-no-shutdown",
            "-drive", f"file={image},format=raw,if=ide,index=0,snapshot=on",
        ] + data_args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        text = ""
        try:
            # 1200 PIT quanta alone take about 12 seconds; the C7 suite (large
            # stacks, 300 KiB ELF, 128-descriptor runs) needs a wider margin.
            limit = time.monotonic() + deadline
            while time.monotonic() < limit:
                text = serial.read_text(errors="replace") if serial.exists() else ""
                if all(marker in text for marker in markers):
                    break
                if "FAIL" in text or "PANIC" in text or process.poll() is not None:
                    break
                time.sleep(0.05)
        finally:
            process.terminate()
            _, stderr = process.communicate(timeout=5)
            log.write_text(text)
        assert all(marker in text for marker in markers), (label, text, stderr.decode(errors="replace"))
        assert "FAIL" not in text and "PANIC" not in text, (label, text)
        if data_image and data_hash:
            assert hashlib.sha256(Path(data_image).read_bytes()).hexdigest() == data_hash, "data disk changed"
        if "UNSUPPORTED CPU" in markers[0]:
            assert "long mode active" not in text, text
        if "[X64] SELFTEST PASS" in markers:
            balance = re.search(r"balance before=(0x[0-9a-f]+) after=(0x[0-9a-f]+)", text)
            assert balance and balance[1] == balance[2], (label, "PMM imbalance", text)
            user_balance = re.search(r"\[USER64\] balance before=(0x[0-9a-f]+) after=(0x[0-9a-f]+)", text)
            assert user_balance and user_balance[1] == user_balance[2], (label, "process PMM imbalance", text)
            elf_balance = re.search(r"\[ELF64\] balance before=(0x[0-9a-f]+) after=(0x[0-9a-f]+)", text)
            assert elf_balance and elf_balance[1] == elf_balance[2], (label, "ELF PMM imbalance", text)
            scheduler_balance = re.search(r"\[SCHED64\] balance before=(0x[0-9a-f]+) after=(0x[0-9a-f]+)", text)
            assert scheduler_balance and scheduler_balance[1] == scheduler_balance[2], (label, "scheduler PMM imbalance", text)
            stress = re.search(r"\[SCHED64\] stress switches=(0x[0-9a-f]+) user ticks=(0x[0-9a-f]+)", text)
            assert stress and int(stress[1], 16) >= 1200 and int(stress[2], 16) >= 1200, (label, "preemption stress", text)
            capacity = re.search(r"\[SCHED64\] capacity=(0x[0-9a-f]+)", text)
            managed = re.search(r"\[MM64\] managed frames=(0x[0-9a-f]+)", text)
            assert managed, (label, "managed RAM report", text)
            managed_pages = int(managed[1], 16)
            expected_capacity = (32 if managed_pages < 96*256 else
                                 64 if managed_pages < 1024*256 else
                                 128 if managed_pages < 8192*256 else
                                 256 if managed_pages < 16384*256 else
                                 512 if managed_pages < 28672*256 else 1024)
            assert capacity and int(capacity[1], 16) == expected_capacity, (label, "RAM-scaled process capacity", text)
            path_balance = re.search(r"\[PATH64\] balance before=(0x[0-9a-f]+) after=(0x[0-9a-f]+) handles=(0x[0-9a-f]+)", text)
            assert path_balance and path_balance[1] == path_balance[2] and int(path_balance[3], 16) == 0, (label, "path ownership balance", text)
            fd_balance = re.search(r"\[FD64\] balance before=(0x[0-9a-f]+) after=(0x[0-9a-f]+) handles=(0x[0-9a-f]+) descriptors=0", text)
            assert fd_balance and fd_balance[1] == fd_balance[2] and int(fd_balance[3], 16) == 0, (label, "file descriptor ownership balance", text)
            stat_balance = re.search(r"\[STAT64\] balance before=(0x[0-9a-f]+) after=(0x[0-9a-f]+) handles=(0x[0-9a-f]+) descriptors=0", text)
            assert stat_balance and stat_balance[1] == stat_balance[2] and int(stat_balance[3], 16) == 0, (label, "metadata ownership balance", text)
            dir_balance = re.search(r"\[DIR64\] balance before=(0x[0-9a-f]+) after=(0x[0-9a-f]+) handles=(0x[0-9a-f]+) descriptors=0", text)
            assert dir_balance and dir_balance[1] == dir_balance[2] and int(dir_balance[3], 16) == 0, (label, "directory ownership balance", text)
            runtime_balance = re.search(r"\[C1\] balance before=(0x[0-9a-f]+) after=(0x[0-9a-f]+) handles=(0x[0-9a-f]+) descriptors=0", text)
            assert runtime_balance and runtime_balance[1] == runtime_balance[2] and int(runtime_balance[3], 16) == 0, (label, "runtime ownership balance", text)
            c2_balance = re.search(r"\[C2\] balance before=(0x[0-9a-f]+) after=(0x[0-9a-f]+) handles=(0x[0-9a-f]+) descriptors=0", text)
            assert c2_balance and c2_balance[1] == c2_balance[2] and int(c2_balance[3], 16) == 0, (label, "C2 memory ownership balance", text)
            c3_balance = re.search(r"\[C3\] balance before=(0x[0-9a-f]+) after=(0x[0-9a-f]+) handles=(0x[0-9a-f]+) descriptors=0", text)
            assert c3_balance and c3_balance[1] == c3_balance[2] and int(c3_balance[3], 16) == 0, (label, "C3 C-runtime ownership balance", text)
            c4_balance = re.search(r"\[C4\] balance before=(0x[0-9a-f]+) after=(0x[0-9a-f]+) handles=(0x[0-9a-f]+) descriptors=0", text)
            assert c4_balance and c4_balance[1] == c4_balance[2] and int(c4_balance[3], 16) == 0, (label, "C4 SDK ownership balance", text)
            c5_balance = re.search(r"\[C5\] balance pmm=(0x[0-9a-f]+) blocks=(0x[0-9a-f]+) inodes=(0x[0-9a-f]+) handles=0", text)
            assert c5_balance and int(c5_balance[1], 16) > 0 and int(c5_balance[2], 16) > 0 and int(c5_balance[3], 16) > 0, (label, "C5 filesystem balance line", text)
            c6_balance = re.search(r"\[C6\] balance pmm=(0x[0-9a-f]+) handles=(0x[0-9a-f]+) slots=(0x[0-9a-f]+) zombies=(0x[0-9a-f]+)", text)
            assert c6_balance and int(c6_balance[2], 16) == 0 and int(c6_balance[3], 16) == 0 and int(c6_balance[4], 16) == 0, (label, "C6 process balance line", text)
            c7_balance = re.search(r"\[C7\] balance pmm=(0x[0-9a-f]+) blocks=(0x[0-9a-f]+) inodes=(0x[0-9a-f]+) handles=(0x[0-9a-f]+) slots=(0x[0-9a-f]+)", text)
            assert c7_balance and int(c7_balance[4], 16) == 0 and int(c7_balance[5], 16) == 0, (label, "C7 compiler-readiness balance", text)
            if any("[SELFHOST] PASS" in marker for marker in markers):
                assert "[SELFHOST] churn cycles=25 PASS" in text, (label, "selfhost churn cycles", text)
                selfhost_balance = re.search(r"\[SELFHOST\] balance pmm=(0x[0-9a-f]+) handles=(0x[0-9a-f]+) slots=(0x[0-9a-f]+) zombies=(0x[0-9a-f]+)", text)
                assert selfhost_balance and int(selfhost_balance[2], 16) == 0 and int(selfhost_balance[3], 16) == 0 and int(selfhost_balance[4], 16) == 0, (label, "selfhost resource balance", text)
            if any("disk full" in marker for marker in markers):
                assert "[C5] diskfull committed=" in text, (label, "disk full ENOSPC observation", text)
            if any("reboot persistence" in marker for marker in markers):
                assert "[C5] PASS: reboot persistence" in text, (label, "reboot persistence", text)
            assert text.count("[sdk] ") >= 30, (label, "SDK example output", text)
            assert "[C4] ABI OK" in text, (label, "C ABI test output", text)
            assert text.count("Hello from PollikOS C") >= 2, (label, "C stdout delivery", text)
            assert "argv[1]=first" in text and "argv[2]=second" in text, (label, "C argv delivery", text)
            assert "[C3] libc and allocator tests OK (0 failures)" in text, (label, "C libc unit tests", text)
            assert "[C3] filesystem tests OK (0 failures)" in text, (label, "C filesystem tests", text)
            assert "[C3] allocator failure injection OK" in text, (label, "C allocation failure injection", text)
            assert text.count("[C3] runtime pid=") >= 104, (label, "C concurrent/lifecycle runtime", text)
            assert text.count("Hello from PollikOS stdout") >= 101 and text.count("Hello from PollikOS stderr") >= 101, (label, "stdio delivery", text)
            assert "W"*8193 in text, (label, "chunked console write delivery", text)
            manifest = json.loads((directory / "data-manifest.json").read_text())
            for name in ("hello", "argvtest", "spin", "faulttest"):
                match = re.search(rf"\[PATH64\] stat /bin/{name} bytes=(0x[0-9a-f]+)", text)
                assert match and int(match[1], 16) == manifest[name]["size"], (name, "disk file size")
        if ram > 4096:
            high = re.search(r"high frame=(0x[0-9a-f]+) high CR3=(0x[0-9a-f]+)", text)
            assert high and int(high[1], 16) >= 2**32 and int(high[2], 16) >= 2**32, text
            stats = re.search(r"managed frames=(0x[0-9a-f]+) above4g=(0x[0-9a-f]+)", text)
            assert stats and int(stats[1], 16)*4096 > 2**32 and int(stats[2], 16) > 0, text
        if ram >= 32768:
            stats = re.search(r"managed frames=(0x[0-9a-f]+) above4g=(0x[0-9a-f]+)", text)
            assert stats and int(stats[1], 16)*4096 >= 30*1024**3, (label, "32 GiB RAM coverage", text)
        print(f"PASS: {label}")
        return text


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-high-memory", action="store_true",
                        help="Explicitly omit 5 GiB and 32 GiB coverage on constrained hosts (not full milestone verification)")
    options = parser.parse_args()
    tests = ["64-bit registers and IRET", "null page", "stack guard",
             "read-only rodata", "read-only text", "NX data", "double-fault IST stack"]
    memory_tests = ["kernel table allocation rollback", "PMM balance and DMA32",
                    "contiguous DMA allocation and free",
                    "dynamic tables, access, unmap and CR3 isolation",
                    "dynamic protections and invalid requests",
                    "borrowed ownership, executable page and guarded stack destruction",
                    "allocation failure rollback", "shared kernel and external MMIO lifetime",
                    "100 mapping lifecycles without PMM leak"]
    user_tests = ["safe copies: ranges, parent permissions, cross-page, strings and failures",
                  "independent CPL3 spaces and per-process kernel stacks",
                  "CPL3 protection faults, ABI errors and safe return validation",
                  "process construction allocation-failure rollback",
                  "100 CPL3 processes without PMM leak"]
    elf_tests = ["26 malformed and unsupported ELF cases without leaks",
                 "ELF RX/RW/NX protections and independent faulting processes",
                 "bounded versioned argc/argv/envp stack construction",
                 "ELF allocation rollback and pre-existing mapping preservation",
                 "100 real ELF64 CPL3 lifecycles without PMM leak"]
    scheduler_tests = ["1200 switches: GPR/RFLAGS, user/kernel stacks and CR3 isolation",
                       "infinite-loop ELFs preempted; current-process kill and safe reaping",
                       "exit, page/GP/UD faults and isolated x87/SSE2 state across sleep",
                       "runnable/blocked kill, duplicate rejection, idle wake and timer stop",
                       "creation/stack/ELF allocation and insertion failure rollback",
                       "100 mixed exit/fault/kill replacements without PMM leak"]
    path_tests = ["PollikFS v2 mount, bin enumeration, stat and seek/read/close",
                  "missing/directory/empty/truncated/large/invalid/dynamic rejection without leaks",
                  "partial reads, early EOF and ATA I/O failure rollback",
                  "buffer/process allocation, full slots and scheduler insertion rollback",
                  "startup ABI v1 argv/envp and diagnostic path ownership",
                  "100 path launches: preemption, exit/fault/kill, PMM and handles balanced"]
    file_tests = ["process-local fd 3, independent offsets, peer close and preemption",
                  "kernel executable handles separated from userspace descriptors",
                  "bad pointers/fds/flags, seek bounds, full table and descriptor reuse",
                  "repeated VFS allocation/read failures preserve handles, offsets and buffers",
                  "exit/fault/kill reclaim all owned descriptors in the safe reaper",
                  "eight processes hold 1000 VFS objects concurrently and reclaim them cleanly",
                  "100 open/read/seek/close/exit lifecycles without PMM or descriptor leaks"]
    stat_tests = ["concurrent fd ownership, preemption and path independence",
                  "bad paths, output pointers and descriptors; no partial output",
                  "stat/fstat IO failures preserve output and file offset",
                  "exit/fault/kill reclaim metadata process descriptors",
                  "dedicated ABI, file/directory types, size, stored ticks and zero reserves",
                  "100 stat/open/fstat/close/exit lifecycles without leaks"]
    dir_tests = ["independent same-directory fd positions across preemption and peer close",
                 "bad paths, pointers, descriptors, full table and slot reuse",
                 "repeated allocation and IO failures preserve output, position and handles",
                 "exit/fault/kill reclaim all owned directory descriptors",
                 "names, types, 55-byte limit, zero padding, empty EOF and rewind",
                 "100 open/enumerate/close/exit lifecycles without leaks"]
    runtime_tests = ["repeated SYSCALL registers, stacks, preemption, cwd and descriptor isolation",
                     "unsafe return RIP/RSP rejected and privileged flags sanitized",
                     "stdio, chunked write, bad buffers/fds and closed streams",
                     "bounded getcwd/chdir, relative paths, dot/dot-dot and failure preservation",
                     "exit/fault/kill reclaim files, streams and cwd state",
                     "100 combined runtime lifecycles without leaks"]
    c2_tests = ["userspace brk grow/shrink/regrow, zeroed pages, mmap/munmap, getpid, clock, sleep",
                "test-only bump allocator: growth, unique patterns, no overlap, shrink and regrow",
                "invalid brk/mmap/munmap requests fail safely and preserve break state",
                "heap pages are RW/NX: executing from heap faults only the offending process",
                "injected heap, page-table and anonymous mapping failures roll back without leaks",
                "multi-process heap isolation at identical virtual addresses across preemption",
                "100 memory lifecycles return PMM exactly to baseline"]
    c3_tests = ["crt0 main(argc,argv), exit-status propagation and stdout",
                "argv/envp and environ from C",
                "libc memory, string and formatter unit tests",
                "malloc/calloc/realloc/free reuse, coalescing, large and overflow paths",
                "injected allocation failure returns NULL and recovers",
                "read-only filesystem, directory and cwd wrappers with errno paths",
                "faulting C process cannot damage a healthy peer",
                "four concurrent C processes: heap and errno isolation across preemption",
                "100 C process lifecycles return PMM exactly to baseline"]
    c4_tests = ["pollikcc-built hello runs from PollikFS, prints and exits 0",
                "SDK filesystem example: stat/open/read/fstat/seek/close",
                "SDK directory example enumerates entries and closes the stream",
                "SDK memory example uses malloc/calloc/realloc/free",
                "SDK cwd example uses getcwd/chdir and a relative path",
                "SDK time example uses the monotonic clock and blocking sleep",
                "SDK errno example reports PollikOS libc errors",
                "multi-file C application compiles and links through pollikcc",
                "static user library builds and links through pollikcc -L/-l",
                "C ABI, calling convention, stack alignment, globals, pointers at -O2 and -O0",
                "stack-guard and invalid-pointer faults contained; healthy C peer continues",
                "optimized C binaries preempted; spin killed at tick limit, peers exit 42",
                "100 SDK-built C process lifecycles return PMM exactly to baseline"]
    c5_tests = ["regular-file create/write/read/verify through the SDK",
                "multi-write offset advance, block boundaries and exact contents",
                "large file crosses direct/indirect blocks and reads back exactly",
                "append writes at current EOF (ABC + DEF)",
                "O_TRUNC releases blocks and resets size",
                "mkdir/rename/read/unlink/rmdir mutation sequence",
                "relative create/rename/unlink through the shared resolver",
                "bad pointers, bad flags and access modes return stable errors",
                "two concurrent writers keep independent descriptors and data",
                "injected allocation failures leave PollikFS mountable and balanced",
                "100 mutation lifecycles return PMM, VFS, block and inode baselines"]
    c6_tests = ["C parent spawns, waits and inspects child exit code 42",
                "child that exits before wait is collected later",
                "wait blocks without busy-spin until child exit",
                "multiple children associate pid and status correctly",
                "faulting child returns fault termination and the parent survives",
                "killed child returns kill termination",
                "invalid spawn and wait requests fail safely",
                "environment inherited by spawn and mutated locally",
                "cwd inherited and independent",
                "descriptor inheritance shares offset and refcounts",
                "close-on-spawn descriptors and three-stage pipe EOF",
                "PATH lookup in libc spawnp",
                "bounded parent/child tree ownership",
                "orphan children auto-reap when the parent exits",
                "process table exhaustion fails cleanly and recovers",
                "injected spawn failures roll back completely",
                "100 spawn/wait lifecycles return PMM exactly to baseline"]
    c7_tests = ["FILE fopen/fclose/fread/fwrite/fseek/ftell/fgets/fputs/ungetc",
                "fopen modes r/w/a/r+/w+/a+ and buffered flush",
                "multi-block FILE seek and overwrite integrity",
                "FILE EOF/error state, remove and access",
                "deep stack frames inside the 256 KiB user stack",
                "128-descriptor limit and cleanup",
                "ctype, string, conversion, qsort/bsearch and strerror",
                "allocator symbol-table and growth stress",
                "three-stage tool chain via spawn/wait and files",
                "eight concurrent compiler-like helpers",
                "temporary files isolate concurrent processes and clean up",
                "large bounded argv arrays reach the helper",
                "300 KiB ELF loads under the raised executable cap",
                "compiler-readiness resources return to baseline"]
    selfhost_markers = ["[SELFHOST] compiler=/bin/tcc",
                        "[SELFHOST] source=/home/hello.c",
                        "[SELFHOST] native compile begin",
                        "[SELFHOST] tcc exit=0",
                        "[SELFHOST] output=/home/hello",
                        "[SELFHOST] output ELF valid",
                        "Hello from self-hosted PollikOS C!",
                        "[SELFHOST] program exit=42",
                        "[SELFHOST] POSIX signals PASS",
                        "[SELFHOST] SIGSTOP/SIGCONT PASS",
                        "[SELFHOST] PROCESS GROUPS PASS",
                        "[SELFHOST] PASS"]
    base_markers = ([f"[X64] PASS: {name}" for name in tests] +
                    [f"[MM64] PASS: {name}" for name in memory_tests] +
                    [f"[USER64] PASS: {name}" for name in user_tests] +
                    [f"[ELF64] PASS: {name}" for name in elf_tests] +
                    [f"[SCHED64] PASS: {name}" for name in scheduler_tests] +
                    [f"[PATH64] PASS: {name}" for name in path_tests] +
                    [f"[FD64] PASS: {name}" for name in file_tests] +
                    [f"[STAT64] PASS: {name}" for name in stat_tests] +
                    [f"[DIR64] PASS: {name}" for name in dir_tests] +
                    [f"[C1] PASS: {name}" for name in runtime_tests] +
                    [f"[C2] PASS: {name}" for name in c2_tests] +
                    [f"[C3] PASS: {name}" for name in c3_tests] +
                    [f"[C4] PASS: {name}" for name in c4_tests] +
                    [f"[C6] PASS: {name}" for name in c6_tests] +
                    [f"[C7] PASS: {name}" for name in c7_tests])
    for ram in (16, 64, 256) + (() if options.skip_high_memory else (5120, 32768)):
        boot("selftest", "qemu64", ram,
             base_markers + selfhost_markers +
             [f"[C5] PASS: {name}" for name in c5_tests] + ["[X64] SELFTEST PASS"],
             data_image=fresh_data("selftest"), mutable_data=True, deadline=900)
    # Reboot persistence: one disposable image booted twice; the second boot
    # must read back files committed by the first.
    reboot_image = fresh_data("selftest", "-reboot")
    boot("selftest", "qemu64", 64,
         base_markers + selfhost_markers +
         [f"[C5] PASS: {name}" for name in c5_tests] + ["[X64] SELFTEST PASS"],
         data_image=reboot_image, suffix="-reboot1", mutable_data=True, deadline=900)
    boot("selftest", "qemu64", 64,
         base_markers + selfhost_markers +
         [f"[C5] PASS: {name}" for name in c5_tests] +
         ["[C5] PASS: reboot persistence: committed files survive a fresh mount",
          "[X64] SELFTEST PASS"],
         data_image=reboot_image, suffix="-reboot2", mutable_data=True, deadline=900)
    # Dedicated almost-full image: the ENOSPC safety path only.
    full_image = BUILD / "selftest" / "PollikData-full.img"
    if full_image.exists():
        run_full = BUILD / "selftest" / "PollikData-full-run.img"
        shutil.copyfile(full_image, run_full)
        boot("selftest", "qemu64", 64,
             base_markers + ["[SELFHOST] skipped: dedicated full image",
                             "[C5] PASS: disk full returns ENOSPC safely and keeps PollikFS mountable",
                             "[X64] SELFTEST PASS"],
             data_image=run_full, suffix="-full", mutable_data=True)
    kernel_markers = ["Hello from ELF64 PollikOS", "[ELF64] real ELF demo exited 42",
         "[SCHED64] demo PASS: three infinite-loop ELFs preempted and killed",
         "[FD64] demo PASS: VFS-launched ELF open/read/seek/close exits 42",
         "[STAT64] demo PASS: VFS-launched ELF stat/fstat exits 42",
         "[DIR64] demo PASS: VFS-launched ELF opendir/readdir/close exits 42",
         "[C1] demo PASS: VFS-launched ELF SYSCALL/stdout/stderr/cwd exits 42",
         "[C2] demo PASS: VFS-launched ELF brk/mmap/getpid/clock/sleep exits 42",
         "[C3] demo PASS: C main(argc,argv) via crt0+libpollikc prints and exits 42",
         "[C4] demo PASS: pollikcc-built C app runs from PollikFS and exits 0",
         "[C5] demo PASS: userspace create/write/read/rename/unlink through the SDK",
         "[C6] demo PASS: C parent spawns, waits and receives child exit 42",
         "[C7] demo PASS: FILE toolchain chain, temp files and 300 KiB ELF load",
         "[sdk] write: notes.txt ->", "Hello from PollikOS C", "Hello from PollikOS C!",
         "argc=0", "[X64] ready (Ring 3, native .pol windows, Desktop/Files Dock apps)"]
    boot("kernel", "qemu64", 64, kernel_markers,
         data_image=fresh_data("kernel"), mutable_data=True)
    boot("kernel", "qemu64", 64, ["[LAUNCH64] /bin/hello VFS exit 42"] + kernel_markers,
         data_image=fresh_data("kernel", "-reboot"), suffix="-reboot", mutable_data=True)
    for cpu in ("pentium3", "qemu64,-lm", "qemu64,-nx", "qemu64,-pae", "qemu64,-msr", "qemu64,-syscall"):
        boot("kernel", cpu, 64, ["[X64] UNSUPPORTED CPU"])
    # Interactive console: real typed commands, TTY stdin and native TinyCC.
    console = subprocess.run([sys.executable, str(ROOT / "tests" / "x86_64_console.py")])
    assert console.returncode == 0, "interactive console workflow failed"
