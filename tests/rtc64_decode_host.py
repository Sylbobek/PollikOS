from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    clang = shutil.which("clang")
    if not clang:
        raise SystemExit("clang is required for the native RTC decode test")
    with tempfile.TemporaryDirectory(prefix="pollikos-rtc64-") as temporary:
        output = Path(temporary) / "rtc64_decode_host.exe"
        subprocess.run([
            clang, "-std=c11", "-Wall", "-Wextra", "-Werror",
            f"-I{ROOT / 'kernel' / 'arch' / 'x86_64'}",
            str(ROOT / "tests" / "rtc64_decode_host.c"),
            str(ROOT / "kernel" / "arch" / "x86_64" / "rtc64_decode.c"),
            "-o", str(output),
        ], cwd=ROOT, check=True)
        subprocess.run([str(output)], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
