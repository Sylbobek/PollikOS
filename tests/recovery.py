"""Failure-path boot tests. Run after smoke.py; only test disk copies are modified."""
import json
import pathlib
import os
import socket
import struct
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def boot_case(name, disk, expected, command=None, contains=None):
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]
    log = BUILD / f"{name}.log"
    log.write_text("")
    args = ["qemu-system-x86_64", "-m", "64M", "-vga", "std",
            "-drive", f"format=raw,file={BUILD / os.environ.get('POLLIK_TEST_IMAGE', 'PollikOS-Alpha.img')},if=ide,index=0",
            "-nic", "none", "-display", "none", "-serial", f"file:{log}",
            "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off"]
    if disk:
        args += ["-drive", f"format=raw,file={disk},if=ide,index=1"]
    process = subprocess.Popen(args, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    try:
        deadline = time.monotonic() + 15
        while True:
            try:
                connection = socket.create_connection(("127.0.0.1", port), timeout=3)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(.1)
        stream = connection.makefile("rwb", buffering=0)
        json.loads(stream.readline())

        def qmp(cmd, arguments=None):
            stream.write((json.dumps({"execute": cmd, "arguments": arguments or {}}) + "\n").encode())
            while True:
                response = json.loads(stream.readline())
                if "error" in response:
                    raise AssertionError(response)
                if "return" in response:
                    return response["return"]

        qmp("qmp_capabilities")
        while "desktop ready" not in log.read_text():
            assert time.monotonic() < deadline, log.read_text()
            time.sleep(.1)
        assert expected in log.read_text(), log.read_text()
        assert "RTL8139: device not found" in log.read_text()
        if command:
            for key in ["f3"] + list(command) + ["ret"]:
                qmp("human-monitor-command", {"command-line": "sendkey " + key})
                time.sleep(.2)
            deadline = time.monotonic() + 5
            while "SHELL END" not in log.read_text():
                assert time.monotonic() < deadline, log.read_text()
                time.sleep(.1)
            assert contains in log.read_text(), log.read_text()
        qmp("quit")
    finally:
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=5)


source = bytearray((BUILD / "smoke-data.img").read_bytes())
assert struct.unpack_from("<I", source, 0)[0] == 0x32534650
assert struct.unpack_from("<I", source, 18 * 512)[0] == 0x32534650
generations = [struct.unpack_from("<I", source, i * 18 * 512 + 4)[0] for i in range(2)]
latest = generations.index(max(generations))
# Last smoke commit deleted test.txt. Previous snapshot must still contain it.
source[(latest * 18 + 1) * 512 + 200] ^= 0x55
fallback = BUILD / "recovery-fallback.img"
fallback.write_bytes(source)
boot_case("recovery-fallback", fallback, "FS mounted persistent snapshot", "ls", "test.txt")
for i in range(2):
    source[i * 18 * 512] ^= 0xff
invalid = BUILD / "recovery-invalid.img"
invalid.write_bytes(source)
boot_case("recovery-invalid", invalid, "FS invalid disk; writes disabled", "save", "Save failed")
assert invalid.read_bytes() == source, "Invalid data disk was modified"
boot_case("recovery-missing", None, "FS no data disk", "save", "Save failed")
print("PASS: previous snapshot recovery, corrupt disk write protection, missing disk/NIC")
