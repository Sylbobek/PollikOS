"""Run native spawn/TinyCC churn at 64 MiB and 5 GiB.

Build with build-x86_64.ps1 -SelfTest first. Each guest receives a disposable
PollikFS copy with the churn count and interleaved TinyCC interval configured.
The defaults preserve the 5,000-cycle regression; --cycles can raise it for
longer investigations without changing the test's pass conditions.
"""
import argparse
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "x86_64"
sys.path.insert(0, str(ROOT / "sdk" / "tools"))
from pollikfs_install import PollikFsImage  # noqa: E402
from x86_64_boot import boot  # noqa: E402


def stress_image(ram, cycles, tcc_interval):
    source = BUILD / "selftest" / "PollikData-test.img"
    target = BUILD / "selftest" / f"PollikData-spawn-stress-{ram}-{cycles}.img"
    if not source.exists():
        raise RuntimeError("missing self-test PollikData image; run build-x86_64.ps1 -SelfTest")
    shutil.copyfile(source, target)
    image = PollikFsImage.load(target)
    settings = {
        "/etc/churn_cycles": f"{cycles}\n".encode("ascii"),
        "/etc/churn_tcc_interval": f"{tcc_interval}\n".encode("ascii"),
    }
    for path, payload in settings.items():
        if image.exists(path):
            raise RuntimeError(f"unexpected pre-existing stress setting {path}")
        image.install_file(path, payload)
    image.save(target)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cycles", type=int, default=5000)
    parser.add_argument("--tcc-interval", type=int, default=200)
    parser.add_argument("--ram", type=int, nargs="+", choices=(64, 5120),
                        default=(64, 5120))
    parser.add_argument("--expect-perturbed", action="store_true",
                        help="require the test-only TIMER64_HZ=137 marker")
    args = parser.parse_args()
    if args.cycles < 5000 or args.tcc_interval < 1:
        parser.error("--cycles must be >= 5000 and --tcc-interval must be positive")
    cycles, interval = args.cycles, args.tcc_interval
    quotient, remainder = divmod(cycles, 5)
    kills = quotient
    faults = quotient + int(remainder >= 4)
    exits = cycles - kills - faults
    tcc_rounds = cycles // interval
    markers = [
        (f"[parent] churn count={cycles} exits={exits} faults={faults} "
         f"kills={kills} tcc={tcc_rounds}"),
        "[C6] PASS: spawn/wait churn (exit/fault/kill mix) returns to baseline",
        f"[SELFHOST] churn cycles={tcc_rounds} PASS",
        "[SELFHOST] PASS",
        "[X64] SELFTEST PASS",
    ]
    if args.expect_perturbed:
        markers.append("[C6] deterministic timer perturbation hz=137")
    for ram in args.ram:
        boot("selftest", "qemu64", ram, markers,
             data_image=stress_image(ram, cycles, interval),
             suffix=f"-spawn-stress-{ram}-{cycles}",
             mutable_data=True, deadline=900)


if __name__ == "__main__":
    main()
