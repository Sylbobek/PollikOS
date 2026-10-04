"""Seeded QEMU crash investigation for x86_64 PollikFS.

Run after build-x86_64.ps1. Each trial starts from a disposable image copy,
launches a guest mutation workload, hard-kills QEMU after a seed-selected
completed mutation, checks raw PollikFS invariants, then boots the kernel
against that image to verify it can remount it.
"""
from pathlib import Path
import json
import random
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "x86_64" / "kernel"
START = 64 * 512
BLOCK = 1024
MAGIC = 0x504B4632
SEEDS = [
    0x00000001, 0x00000013, 0x0000002B, 0x00000055,
    0x00000089, 0x000000C3, 0x00000127, 0x000001F1,
    0x0000035B, 0x00000567, 0x00000ABC, 0x00000DEF,
    0x00001234, 0x00005150, 0x0000CAFE, 0x00005EED,
    0x002CB31B, 0x000DDCEA, 0x00C12BA3, 0x004D82C7,
    0x006491D5, 0x004640AC, 0x0085ED1C, 0x0030D2E1,
    0x00D3EFBC, 0x00BE466B, 0x00B4A7D9, 0x00ABE251,
    0x00207288, 0x0079DB89, 0x0025154C, 0x00F20F80,
]
OP_SEQUENCE = (
    "create", "write", "append", "mkdir", "create_inner",
    "write_inner", "rename", "unlink", "unlink_inner", "rmdir",
)


def add_error(errors, category, message):
    errors.setdefault(category, []).append(message)


def check_invariants(image_path):
    errors = {}
    try:
        data = Path(image_path).read_bytes()
    except OSError as error:
        return {"image_io": [str(error)]}, {}
    if len(data) < START + BLOCK:
        return {"superblock": ["image is too short"]}, {}
    try:
        fields = struct.unpack_from("<13I", data, START)
    except struct.error as error:
        return {"superblock": [str(error)]}, {}
    (magic, block_size, total_blocks, inode_count, free_blocks, free_inodes,
     root_inode, bitmap_block, bitmap_count, inode_table_block,
     inode_table_count, data_start, _generation) = fields
    if magic != MAGIC or block_size != BLOCK:
        return {"superblock": [f"bad magic/block size: {magic:#x}/{block_size}"]}, {}
    if (total_blocks != 32768 or inode_count != 512 or root_inode <= 0 or
            root_inode >= inode_count or bitmap_block != 1 or bitmap_count != 4 or
            inode_table_block != 5 or inode_table_count != 31 or data_start != 36):
        return {"superblock": [f"unsupported geometry: {fields}"]}, {}
    if START + total_blocks * BLOCK > len(data):
        return {"superblock": ["filesystem region exceeds image length"]}, {}

    bitmap_offset = START + bitmap_block * BLOCK
    bitmap = data[bitmap_offset:bitmap_offset + bitmap_count * BLOCK]
    allocated = {
        block for block in range(total_blocks)
        if block // 8 < len(bitmap) and (bitmap[block // 8] >> (block % 8)) & 1
    }

    inodes = [None] * inode_count
    bad_inode_modes = []
    for number in range(1, inode_count):
        offset = START + (inode_table_block + number // 17) * BLOCK + (number % 17) * 60
        if offset + 60 > len(data):
            add_error(errors, "inode_table", f"inode {number}: record outside image")
            continue
        values = struct.unpack_from("<15I", data, offset)
        mode, size = values[:2]
        direct = values[2:10]
        indirect = values[10]
        double_indirect = values[13]
        inodes[number] = {
            "mode": mode, "size": size, "direct": direct,
            "indirect": indirect, "double": double_indirect,
        }
        if mode not in (0, 1, 2):
            bad_inode_modes.append(f"inode {number}: mode {mode}")
    if bad_inode_modes:
        errors["inode_mode"] = bad_inode_modes

    actual_free_inodes = sum(
        1 for number in range(1, inode_count)
        if inodes[number] is not None and inodes[number]["mode"] == 0
    )
    if free_inodes != actual_free_inodes:
        add_error(errors, "free_counters",
                  f"free_inodes={free_inodes}, actual={actual_free_inodes}")
    actual_free_blocks = total_blocks - len(allocated)
    if free_blocks != actual_free_blocks:
        add_error(errors, "free_counters",
                  f"free_blocks={free_blocks}, actual={actual_free_blocks}")

    owners = {}
    referenced = set()

    def claim(number, block, role):
        if not block:
            return
        if block < data_start or block >= total_blocks:
            add_error(errors, "block_reference_range",
                      f"inode {number} {role} references out-of-range block {block}")
            return
        previous = owners.get(block)
        owner = f"inode {number} {role}"
        if previous is not None:
            add_error(errors, "duplicate_block_references",
                      f"block {block}: {previous} and {owner}")
        else:
            owners[block] = owner
        referenced.add(block)

    def pointer_values(block, number, role):
        if not block or block < data_start or block >= total_blocks:
            return []
        offset = START + block * BLOCK
        return struct.unpack_from("<256I", data, offset)

    logical_blocks = {}
    for number in range(1, inode_count):
        inode = inodes[number]
        if inode is None or inode["mode"] == 0:
            continue
        mode, size = inode["mode"], inode["size"]
        if size > total_blocks * BLOCK:
            add_error(errors, "inode_size", f"inode {number}: size {size} exceeds filesystem")
        direct = list(inode["direct"])
        for index, block in enumerate(direct):
            claim(number, block, f"direct[{index}]")
        data_sequence = direct[:]

        indirect = inode["indirect"]
        if indirect:
            claim(number, indirect, "indirect_table")
            values = list(pointer_values(indirect, number, "indirect"))
            data_sequence.extend(values)
            for index, block in enumerate(values):
                claim(number, block, f"indirect[{index}]")

        double = inode["double"]
        if double:
            claim(number, double, "double_indirect_table")
            outer = list(pointer_values(double, number, "double_indirect"))
            for outer_index, middle in enumerate(outer):
                if not middle:
                    data_sequence.extend([0] * 256)
                    continue
                claim(number, middle, f"double_indirect[{outer_index}]")
                values = list(pointer_values(middle, number, "double_indirect_data"))
                data_sequence.extend(values)
                for inner_index, block in enumerate(values):
                    claim(number, block,
                          f"double_indirect[{outer_index}][{inner_index}]")

        needed = (size + BLOCK - 1) // BLOCK
        logical_blocks[number] = data_sequence[:needed]
        if mode == 2:
            if size % BLOCK:
                add_error(errors, "directory_entries",
                          f"inode {number}: directory size {size} is not block aligned")
            if needed > len(logical_blocks[number]):
                add_error(errors, "directory_entries",
                          f"inode {number}: missing directory block references")

    reserved = set(range(data_start))
    expected_allocated = reserved | referenced
    missing_bits = sorted(expected_allocated - allocated)
    orphan_bits = sorted(allocated - expected_allocated)
    if missing_bits:
        add_error(errors, "bitmap_vs_inode_references",
                  f"referenced/reserved blocks with clear bitmap bits: {missing_bits[:24]}")
    if orphan_bits:
        add_error(errors, "bitmap_vs_inode_references",
                  f"allocated blocks with no live inode reference: {orphan_bits[:24]}")

    bad_entries = []
    for directory_number in range(1, inode_count):
        inode = inodes[directory_number]
        if inode is None or inode["mode"] != 2:
            continue
        size = inode["size"]
        blocks = logical_blocks.get(directory_number, [])
        seen_names = set()
        entries_to_read = size // 64
        for entry_index in range(entries_to_read):
            block_index = (entry_index * 64) // BLOCK
            in_block = (entry_index * 64) % BLOCK
            if block_index >= len(blocks) or not blocks[block_index]:
                bad_entries.append(
                    f"directory inode {directory_number}: entry {entry_index} has no data block")
                continue
            block = blocks[block_index]
            offset = START + block * BLOCK + in_block
            if offset + 64 > len(data):
                bad_entries.append(
                    f"directory inode {directory_number}: entry {entry_index} outside image")
                continue
            target, record_length, name_length, file_type, name = struct.unpack_from(
                "<IHBB56s", data, offset)
            if target == 0:
                continue
            if record_length != 64 or name_length == 0 or name_length > 55:
                bad_entries.append(
                    f"directory inode {directory_number}: malformed entry {entry_index}")
                continue
            if name[name_length] != 0 or b"\x00" in name[:name_length] or b"/" in name[:name_length]:
                bad_entries.append(
                    f"directory inode {directory_number}: invalid name at entry {entry_index}")
            if target >= inode_count or inodes[target] is None or inodes[target]["mode"] == 0:
                bad_entries.append(
                    f"directory inode {directory_number}: entry {entry_index} targets invalid inode {target}")
                continue
            if file_type != inodes[target]["mode"]:
                bad_entries.append(
                    f"directory inode {directory_number}: entry {entry_index} type {file_type} "
                    f"does not match inode {target} mode {inodes[target]['mode']}")
            key = name[:name_length]
            if key in seen_names:
                bad_entries.append(
                    f"directory inode {directory_number}: duplicate name {key!r}")
            seen_names.add(key)
    if bad_entries:
        errors["directory_entries"] = bad_entries[:48]

    summary = {
        "allocated_blocks": len(allocated),
        "referenced_data_and_indirect_blocks": len(referenced),
        "live_inodes": sum(1 for inode in inodes[1:]
                           if inode is not None and inode["mode"] != 0),
        "free_blocks_stored": free_blocks,
        "free_blocks_actual": actual_free_blocks,
        "free_inodes_stored": free_inodes,
        "free_inodes_actual": actual_free_inodes,
    }
    return errors, summary


class SerialConsole:
    def __init__(self, stream, process):
        self.stream = stream
        self.process = process
        self.transcript = bytearray()

    def pump(self, timeout=0.01):
        self.stream.settimeout(timeout)
        try:
            chunk = self.stream.recv(65536)
        except (socket.timeout, TimeoutError):
            return
        if chunk:
            self.transcript.extend(chunk)

    def send(self, text):
        self.stream.sendall(text.encode("utf-8") + b"\r")

    def wait_for(self, needle, timeout=90):
        pattern = needle.encode("utf-8")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if pattern in self.transcript:
                return
            if b"CRASH_MUTATOR_FAIL" in self.transcript:
                raise RuntimeError(self.text()[-2000:])
            if self.process.poll() is not None:
                raise RuntimeError(f"QEMU exited while waiting for {needle!r}")
            self.pump()
        raise TimeoutError(f"serial timeout waiting for {needle!r}: {self.text()[-2000:]}")

    def text(self):
        return self.transcript.decode("utf-8", "replace")


def serial_socket(process, port):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("QEMU exited before serial connection")
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=1)
        except OSError:
            time.sleep(0.05)
    raise TimeoutError("QEMU serial socket did not accept a connection")


def stop_qemu(process):
    if process.poll() is None:
        process.kill()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def qemu_args(kernel_image, data_image, serial_arg, snapshot_data=False):
    data_drive = f"file={data_image},format=raw,if=ide,index=1"
    if snapshot_data:
        data_drive += ",snapshot=on"
    return [
        "qemu-system-x86_64", "-accel", "tcg", "-machine", "pc",
        "-cpu", "qemu64", "-m", "64", "-vga", "std", "-nic", "none",
        "-display", "none", "-monitor", "none", "-serial", serial_arg,
        "-no-reboot", "-no-shutdown",
        "-drive", f"file={kernel_image},format=raw,if=ide,index=0,snapshot=on",
        "-drive", data_drive,
    ]


def start_trial(kernel_image, data_image, username, password, target_step,
                target_operation, delay_ms, log_dir, seed):
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]
    process = subprocess.Popen(
        qemu_args(kernel_image, data_image,
                  f"tcp:127.0.0.1:{port},server=on,nowait"),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    stream = None
    console = None
    captured = b""
    try:
        stream = serial_socket(process, port)
        console = SerialConsole(stream, process)
        console.wait_for("[X64] ready (Ring 3, native .pol windows, Desktop/Files Dock apps)")
        console.wait_for("[AUTH64] First run: create a local account.")
        console.send(username)
        console.wait_for("Create password (6-63 characters):")
        console.send(password)
        console.wait_for("Confirm password:")
        console.send(password)
        console.wait_for("Account created.")
        console.wait_for("[terminal] shell connected through PollikOS pipes")
        console.wait_for("PollikOS:/>")
        console.send("/bin/crashmut")
        marker = f"CRASH_STEP {target_step} {target_operation}".encode()
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            if marker in console.transcript:
                break
            if b"CRASH_MUTATOR_FAIL" in console.transcript:
                raise RuntimeError("guest mutator reported failure")
            if process.poll() is not None:
                raise RuntimeError("QEMU exited before the selected mutation")
            console.pump()
        else:
            raise TimeoutError(f"guest never reached CRASH_STEP {target_step}")
        step_pattern = re.compile(rb"CRASH_STEP (\d+) ([a-z_]+)")
        match = step_pattern.search(console.transcript)
        while match and int(match.group(1)) != target_step:
            match = step_pattern.search(console.transcript, match.end())
        if not match:
            raise RuntimeError(f"could not decode target step {target_step}")
        completed = match.group(2).decode("ascii")
        time.sleep(delay_ms / 1000.0)
        captured = bytes(console.transcript)
        stop_qemu(process)
        (log_dir / f"serial-seed-{seed:08x}.log").write_bytes(captured)
        return completed
    finally:
        if stream is not None:
            stream.close()
        stop_qemu(process)
        if console is not None and not captured:
            (log_dir / f"serial-seed-{seed:08x}.log").write_text(
                console.text(), encoding="utf-8")


def remount_check(kernel_image, data_image, log_dir, seed):
    serial_log = log_dir / f"remount-seed-{seed:08x}.log"
    process = subprocess.Popen(
        qemu_args(kernel_image, data_image, f"file:{serial_log}",
                  snapshot_data=True),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    mounted = False
    deadline = time.monotonic() + 45
    try:
        while time.monotonic() < deadline:
            text = serial_log.read_text(errors="replace") if serial_log.exists() else ""
            if "PollikFS v2 superblock mounted successfully" in text:
                mounted = True
                break
            if "[VFS64] mount refused" in text or "mount refused" in text:
                break
            if process.poll() is not None:
                break
            time.sleep(0.02)
    finally:
        stop_qemu(process)
    text = serial_log.read_text(errors="replace") if serial_log.exists() else ""
    return mounted, text[-1000:]


def main():
    source_image = BUILD / "PollikData-test.img"
    runtime = BUILD / "sdk"
    kernel_candidates = sorted(
        BUILD.glob("PollikOS-x86_64*.img"),
        key=lambda path: path.stat().st_mtime, reverse=True)
    if not source_image.exists() or not runtime.exists() or not kernel_candidates:
        raise SystemExit("build-x86_64.ps1 must complete before this test")
    kernel_image = kernel_candidates[0]
    powershell = shutil.which("powershell") or shutil.which("pwsh")
    if not powershell:
        raise SystemExit("PowerShell is required to build the PollikOS guest fixture")

    output_dir = BUILD / "crash-consistency"
    output_dir.mkdir(parents=True, exist_ok=True)
    result_path = output_dir / "results.jsonl"
    mutator = output_dir / "crashmut.elf"
    template = output_dir / "PollikData-mutator-template.img"
    trial_image = output_dir / "PollikData-trial.img"
    subprocess.run([
        powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
        str(ROOT / "sdk" / "tools" / "pollikcc.ps1"), "--runtime", str(runtime),
        str(ROOT / "tests" / "x86_64_crash_mutator.c"), "-o", str(mutator),
    ], cwd=ROOT, check=True)
    shutil.copyfile(source_image, template)
    subprocess.run([
        sys.executable, str(ROOT / "sdk" / "tools" / "pollikinstall.py"),
        str(template), "/bin/crashmut", str(mutator),
    ], cwd=ROOT, check=True)
    baseline_errors, baseline_summary = check_invariants(template)
    if baseline_errors:
        raise SystemExit(f"prepared baseline image already violates invariants: {baseline_errors}")

    rows = []
    log_dir = output_dir / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    with result_path.open("w", encoding="utf-8") as report:
        report.write(json.dumps({
            "fixed_seed_list": [f"0x{seed:08x}" for seed in SEEDS],
            "kernel_image": str(kernel_image),
            "baseline": baseline_summary,
        }) + "\n")
        print("fixed seeds: " + ", ".join(f"0x{seed:08x}" for seed in SEEDS))
        print(f"kernel: {kernel_image}")
        print(f"baseline invariants: PASS {baseline_summary}")
        for seed in SEEDS:
            rng = random.Random(seed)
            target_step = rng.randint(1, 40)
            delay_ms = rng.randrange(0, 80)
            target_op = OP_SEQUENCE[(target_step - 1) % len(OP_SEQUENCE)]
            shutil.copyfile(template, trial_image)
            try:
                completed = start_trial(
                    kernel_image, trial_image, "crashuser", "crashpass123",
                    target_step, target_op, delay_ms, log_dir, seed)
                errors, summary = check_invariants(trial_image)
            except Exception as error:
                completed = "harness-error"
                errors = {"harness": [f"{type(error).__name__}: {error}"]}
                summary = {}
                mounted = False
            else:
                if completed != target_op:
                    add_error(errors, "workload",
                              f"step {target_step}: expected {target_op}, observed {completed}")
                try:
                    mounted, mount_log = remount_check(
                        kernel_image, trial_image, log_dir, seed)
                    if not mounted:
                        add_error(errors, "kernel_remount",
                                  mount_log.strip() or
                                  "kernel did not report a successful mount")
                except Exception as error:
                    mounted = False
                    add_error(errors, "kernel_remount",
                              f"{type(error).__name__}: {error}")
            row = {
                "seed": f"0x{seed:08x}",
                "target_step": target_step,
                "target_operation": target_op,
                "completed_operation": completed,
                "random_post_marker_delay_ms": delay_ms,
                "kernel_remount": mounted,
                "summary": summary,
                "invariant_breaks": errors,
            }
            rows.append(row)
            report.write(json.dumps(row, sort_keys=True) + "\n")
            report.flush()
            state = "PASS" if not errors and mounted else "BREAK"
            print(f"{state}: seed=0x{seed:08x} step={target_step} "
                  f"operation={completed} remount={'PASS' if mounted else 'FAIL'}",
                  flush=True)
            for category, messages in errors.items():
                for message in messages:
                    print(f"  {category}: {message}", flush=True)

    failures = sum(bool(row["invariant_breaks"]) for row in rows)
    print(f"crash consistency trials={len(rows)} failures={failures}")
    print(f"results: {result_path}")
    if failures:
        raise SystemExit(1)
    print("PASS: all seeded crash images remounted with filesystem invariants intact")


if __name__ == "__main__":
    main()
