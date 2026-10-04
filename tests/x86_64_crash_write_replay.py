"""Replay captured PollikFS sector writes as prefixes, lost writes, and tears.

Build the normal x86_64 kernel with ``build-x86_64.ps1 -CrashWriteLog`` first.
The test records successful guest ATA sector writes through a test-only linker
wrapper, then reconstructs each seeded disk image from a pristine fixture.
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
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "x86_64" / "kernel"
sys.path.insert(0, str(ROOT / "tests"))
import x86_64_crash_consistency as cc  # noqa: E402

FRAME_SIZE = 4 + 4 + 4 + 512
MAX_STEPS = 13 * 12
SEED_BASE = 0x5EED0000
MODE_TAG = {"prefix": 0x50524658, "drop": 0x44524F50,
            "half": 0x48414C46, "reorder": 0x52454F52}
OP_SEQUENCE = ("create", "write", "append", "mkdir", "create_inner",
               "write_inner", "create_target", "write_target", "rename",
               "rename_replace", "unlink", "unlink_inner", "rmdir")


def parse_log(path):
    raw = Path(path).read_bytes()
    records = []
    offset = 0
    while offset + FRAME_SIZE <= len(raw):
        if raw[offset:offset + 4] != b"PKWL":
            found = raw.find(b"PKWL", offset + 1)
            if found < 0:
                break
            offset = found
            continue
        sequence, lba = struct.unpack_from("<II", raw, offset + 4)
        records.append({"sequence": sequence, "lba": lba,
                        "data": raw[offset + 12:offset + FRAME_SIZE]})
        offset += FRAME_SIZE
    return records, len(raw), offset


def frame_count_when_stable(path):
    previous = -1
    stable = 0
    deadline = time.monotonic() + 0.10
    while time.monotonic() < deadline:
        size = Path(path).stat().st_size if Path(path).exists() else 0
        if size == previous:
            stable += 1
            if stable >= 3 and size % FRAME_SIZE == 0:
                return size // FRAME_SIZE
        else:
            stable = 0
            previous = size
        time.sleep(0.01)
    size = Path(path).stat().st_size if Path(path).exists() else 0
    if size % FRAME_SIZE:
        raise RuntimeError(f"debugcon write log ended mid-frame at {size} bytes")
    return size // FRAME_SIZE


def capture_log(kernel, template, output_dir, max_steps):
    powershell = shutil.which("powershell") or shutil.which("pwsh")
    if not powershell:
        raise RuntimeError("PowerShell is required to build the guest mutator")
    runtime = BUILD / "sdk"
    mutator = output_dir / "crashmut.elf"
    subprocess.run([
        powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
        str(ROOT / "sdk" / "tools" / "pollikcc.ps1"), "--runtime", str(runtime),
        str(ROOT / "tests" / "x86_64_crash_mutator.c"), "-o", str(mutator),
    ], cwd=ROOT, check=True)
    subprocess.run([
        sys.executable, str(ROOT / "sdk" / "tools" / "pollikinstall.py"),
        str(template), "/bin/crashmut", str(mutator),
    ], cwd=ROOT, check=True)

    trial = output_dir / "PollikData-capture.img"
    shutil.copyfile(template, trial)
    debugcon = output_dir / "sector-writes.bin"
    if debugcon.exists():
        debugcon.unlink()
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]
    args = cc.qemu_args(kernel, trial,
                        f"tcp:127.0.0.1:{port},server=on,nowait")
    args += ["-debugcon", f"file:{debugcon}", "-global",
             "isa-debugcon.iobase=0xe9"]
    process = subprocess.Popen(
        args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    stream = None
    console = None
    boundaries = []
    try:
        stream = cc.serial_socket(process, port)
        console = cc.SerialConsole(stream, process)
        console.wait_for("[X64] ready (Ring 3, native .pol windows, Desktop/Files Dock apps)")
        console.wait_for("[AUTH64] First run: create a local account.")
        console.send("replayuser")
        console.wait_for("Create password (6-63 characters):")
        console.send("replaypass123")
        console.wait_for("Confirm password:")
        console.send("replaypass123")
        console.wait_for("Account created.")
        console.wait_for("[terminal] shell connected through PollikOS pipes")
        console.wait_for("PollikOS:/>")
        console.send(f"/bin/crashmut {max_steps}")
        marker_re = re.compile(rb"CRASH_STEP (\d+) ([a-z_]+)")
        recorded = 0
        deadline = time.monotonic() + 240
        while recorded < max_steps and time.monotonic() < deadline:
            if b"CRASH_MUTATOR_FAIL" in console.transcript:
                raise RuntimeError("guest crash mutator failed: " + console.text()[-1200:])
            if process.poll() is not None:
                raise RuntimeError("QEMU exited during sector-log capture")
            matches = list(marker_re.finditer(console.transcript))
            while recorded < len(matches):
                match = matches[recorded]
                step = int(match.group(1))
                if step != recorded + 1:
                    raise RuntimeError(f"mutation marker gap: expected {recorded + 1}, saw {step}")
                count = frame_count_when_stable(debugcon)
                expected_operation = OP_SEQUENCE[(step - 1) % len(OP_SEQUENCE)]
                observed_name = match.group(2).decode("ascii")
                if not expected_operation.startswith(observed_name):
                    raise RuntimeError(f"mutation marker {step}: expected "
                                       f"{expected_operation}, observed {observed_name}")
                boundaries.append({"step": step,
                                   "operation": expected_operation,
                                   "records": count})
                recorded += 1
            console.pump(0.01)
        if recorded != max_steps:
            raise TimeoutError(f"captured {recorded}/{max_steps} mutation markers")
        frame_count_when_stable(debugcon)
    finally:
        if stream is not None:
            stream.close()
        cc.stop_qemu(process)
    records, raw_bytes, parsed_bytes = parse_log(debugcon)
    sequences = [record["sequence"] for record in records]
    if sequences != list(range(len(records))):
        raise RuntimeError(f"write-log sequence is not contiguous: {sequences[:8]}... "
                           f"count={len(sequences)}")
    for record in records:
        if record["lba"] * 512 + 512 > Path(template).stat().st_size:
            raise RuntimeError(f"write-log LBA {record['lba']} exceeds test disk")
    if not records:
        raise RuntimeError("no ATA sector writes were captured")
    return trial, records, boundaries, raw_bytes, parsed_bytes


def apply_variant(template, target, records, boundary_count, mode, rng):
    if not 0 <= boundary_count <= len(records):
        raise RuntimeError(f"bad replay prefix {boundary_count}/{len(records)}")
    chosen = records[:boundary_count]
    mutated_index = None
    if mode in ("drop", "half") and chosen:
        tail = min(4, len(chosen))
        mutated_index = len(chosen) - 1 - rng.randrange(tail)
    if mode == "reorder" and len(chosen) >= 2:
        tail = min(4, len(chosen))
        first = len(chosen) - tail + rng.randrange(tail - 1)
        chosen = list(chosen)
        chosen[first], chosen[first + 1] = chosen[first + 1], chosen[first]
        mutated_index = first
    shutil.copyfile(template, target)
    with open(target, "r+b", buffering=0) as image:
        for index, record in enumerate(chosen):
            if index == mutated_index and mode == "drop":
                continue
            image.seek(record["lba"] * 512)
            if index == mutated_index and mode == "half":
                image.write(record["data"][:256])
            else:
                image.write(record["data"])
    return mutated_index


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seeds-per-mode", type=int, default=200)
    parser.add_argument("--capture-steps", type=int, default=MAX_STEPS,
                        help="mutator boundaries to capture; multiple of 13")
    parser.add_argument("--capture-only", action="store_true",
                        help="validate sector framing without replaying seed modes")
    args = parser.parse_args()
    if args.capture_steps < 13 or args.capture_steps % 13:
        parser.error("--capture-steps must be a positive multiple of 13")
    if not args.capture_only and args.seeds_per_mode < 200:
        parser.error("--seeds-per-mode must be at least 200 for the requested coverage")

    output_dir = BUILD / "crash-write-replay"
    output_dir.mkdir(parents=True, exist_ok=True)
    source_image = BUILD / "PollikData-test.img"
    kernel = BUILD / "PollikOS-x86_64.img"
    if not source_image.exists() or not kernel.exists() or not (BUILD / "sdk").exists():
        raise SystemExit("build-x86_64.ps1 -CrashWriteLog must complete first")
    template = output_dir / "PollikData-template.img"
    shutil.copyfile(source_image, template)
    baseline_errors, baseline = cc.check_invariants(template)
    if baseline_errors:
        raise SystemExit(f"baseline image violates invariants: {baseline_errors}")
    capture_trial, records, boundaries, raw_bytes, parsed_bytes = capture_log(
        kernel, template, output_dir, args.capture_steps)
    baseline_errors, baseline = cc.check_invariants(template)
    if baseline_errors:
        raise SystemExit(f"prepared replay baseline violates invariants: {baseline_errors}")
    reconstructed = output_dir / "PollikData-reconstructed.img"
    apply_variant(template, reconstructed, records, len(records), "prefix",
                  random.Random(0))
    if reconstructed.read_bytes() != capture_trial.read_bytes():
        raise SystemExit("replayed complete write log does not match guest disk image")
    # Preserve a standalone copy of the raw log and capture metadata for audit.
    raw_log = output_dir / "sector-writes.bin"
    boundary_path = output_dir / "operation-boundaries.json"
    boundary_path.write_text(json.dumps(boundaries, indent=2), encoding="utf-8")
    parsed_path = output_dir / "write-log.jsonl"
    with parsed_path.open("w", encoding="utf-8") as stream:
        for record in records:
            stream.write(json.dumps({"sequence": record["sequence"],
                                     "lba": record["lba"],
                                     "data_hex": record["data"].hex()}) + "\n")
    trial = output_dir / "PollikData-replay-trial.img"
    result_path = output_dir / "results.jsonl"
    seeds = [SEED_BASE + index for index in range(args.seeds_per_mode)]
    modes = ("prefix", "drop", "half", "reorder")
    boundary_choices = [boundary for boundary in boundaries if boundary["records"] > 0]
    if not boundary_choices:
        raise SystemExit("captured operations without any completed sector writes")
    print(f"write-log raw_bytes={raw_bytes} parsed_bytes={parsed_bytes} "
          f"frames={len(records)} sequence=0..{len(records)-1}")
    print(f"workload markers={len(boundaries)} operations=" +
          ",".join(sorted({item['operation'] for item in boundaries})))
    if args.capture_only:
        print("PASS: sector write framing and sequence validation")
        return
    print(f"fixed seed list: {', '.join(f'0x{seed:08x}' for seed in seeds)}")
    print(f"baseline invariants: {baseline}")
    rows = []
    with result_path.open("w", encoding="utf-8") as report:
        report.write(json.dumps({
            "fixed_seeds": [f"0x{seed:08x}" for seed in seeds],
            "modes": list(modes), "kernel": str(kernel),
            "captured_write_log": str(raw_log),
            "parsed_write_log": str(parsed_path),
            "operation_boundaries": str(boundary_path),
            "captured_trial_after_guest_run": str(capture_trial),
            "raw_log_bytes": raw_bytes, "parsed_log_bytes": parsed_bytes,
            "frame_count": len(records), "mutation_markers": boundaries,
            "baseline": baseline,
        }) + "\n")
        for mode in modes:
            for ordinal, seed in enumerate(seeds):
                rng = random.Random(seed ^ MODE_TAG[mode])
                boundary = (boundary_choices[ordinal % len(boundary_choices)]
                            if ordinal < len(boundary_choices)
                            else rng.choice(boundary_choices))
                boundary_count = boundary["records"]
                if mode == "prefix":
                    # Prefix mode chooses exact sector-log boundaries sampled
                    # across the deterministic mutation operation sequence.
                    applied = boundary_count
                else:
                    applied = boundary_count
                modified = apply_variant(template, trial, records, applied,
                                         mode, rng)
                invariant_errors, summary = cc.check_invariants(trial)
                try:
                    mounted, mount_log = cc.remount_check(
                        kernel, trial, output_dir, seed ^ MODE_TAG[mode])
                except Exception as error:
                    mounted = False
                    mount_log = f"{type(error).__name__}: {error}"
                if not mounted:
                    invariant_errors.setdefault("kernel_remount", []).append(
                        mount_log.strip() or "kernel did not report a successful mount")
                row = {
                    "mode": mode, "seed": f"0x{seed:08x}",
                    "step": boundary["step"], "operation": boundary["operation"],
                    "prefix_records": applied, "modified_record_index": modified,
                    "mounted": mounted, "invariant_breaks": invariant_errors,
                    "summary": summary,
                }
                rows.append(row)
                report.write(json.dumps(row, sort_keys=True) + "\n")
                report.flush()
                state = "INTACT" if not invariant_errors else "BREAK"
                print(f"{state}: mode={mode} seed=0x{seed:08x} "
                      f"step={boundary['step']} op={boundary['operation']} "
                      f"prefix={applied} modified={modified} "
                      f"mount={'yes' if mounted else 'no'} "
                      f"invariants={','.join(sorted(invariant_errors)) or 'none'}",
                      flush=True)
    print(f"replay totals seeds_per_mode={args.seeds_per_mode} "
          f"trials={len(rows)} breaks={sum(bool(row['invariant_breaks']) for row in rows)}")
    for mode in modes:
        mode_rows = [row for row in rows if row["mode"] == mode]
        mounts = sum(row["mounted"] for row in mode_rows)
        mode_breaks = sum(bool(row["invariant_breaks"]) for row in mode_rows)
        print(f"summary mode={mode} seeds={len(mode_rows)} breaks={mode_breaks} "
              f"guest_mounts={mounts}/{len(mode_rows)}")
        for operation in OP_SEQUENCE:
            selected = [row for row in mode_rows if row["operation"] == operation]
            broken = sum(bool(row["invariant_breaks"]) for row in selected)
            print(f"  operation={operation} seeds={len(selected)} breaks={broken}")
    print(f"write log: {raw_log}")
    print(f"results: {result_path}")


if __name__ == "__main__":
    main()
