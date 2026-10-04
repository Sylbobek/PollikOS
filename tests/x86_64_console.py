"""Drive the x86_64 interactive console over a TCP serial link.

Boots the normal (non-selftest) kernel, connects to the COM1 socket and types a
real shell session, including the native TinyCC workflow:

    cd /home; cat hello.c; tcc hello.c -o hello.pol; ./hello.pol; echo $?

The host never compiles the proof program: /bin/tcc does it inside the guest.
"""
from pathlib import Path
import argparse
import shutil
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "x86_64" / "kernel"
PROMPT = b"\rPollikOS:"
ENTER = b"\r"


class Console:
    def __init__(self, stream):
        self.stream = stream
        self.transcript = b""

    def pump(self, seconds=0.1):
        self.stream.settimeout(seconds)
        try:
            data = self.stream.recv(65536)
        except (socket.timeout, TimeoutError):
            return False
        if data:
            self.transcript += data
            return True
        return False

    def type_text(self, text):
        self.stream.sendall(text.encode("utf-8", "replace"))

    def type_bytes(self, data):
        self.stream.sendall(data)

    def enter(self):
        self.stream.sendall(ENTER)

    def send(self, command):
        self.type_text(command)
        self.enter()

    def wait_for(self, needle, timeout=60, start=0):
        deadline = time.monotonic() + timeout
        pattern = needle.encode("utf-8")
        while time.monotonic() < deadline:
            index = self.transcript.find(pattern, start)
            if index >= 0:
                return index
            self.pump(0.1)
        raise AssertionError(f"console timeout waiting for {needle!r}\n{self.text()[-2000:]}")

    def wait_for_prompt(self, timeout=60, start=0):
        """Wait for a prompt that follows the most recent Enter echo."""
        deadline = time.monotonic() + timeout
        idle = 0
        while time.monotonic() < deadline:
            changed = self.pump(0.1)
            if changed:
                idle = 0
                continue
            enter = self.transcript.rfind(b"\r\n")
            prompt = self.transcript.find(PROMPT, max(start, enter))
            if prompt >= 0:
                idle += 1
                if idle >= 2:
                    return prompt
            else:
                idle = 0
        raise AssertionError(f"console timeout waiting for a prompt\n{self.text()[-2000:]}")

    def run(self, command, expected=None, timeout=120):
        start = len(self.transcript)
        self.send(command)
        if expected is not None:
            self.wait_for(expected, timeout=timeout, start=start)
        return self.wait_for_prompt(timeout=timeout, start=start)

    def text(self):
        return self.transcript.decode("utf-8", "replace")


def newest_image(directory):
    images = sorted(directory.glob("PollikOS-x86_64*.img"),
                    key=lambda path: path.stat().st_mtime, reverse=True)
    assert images, f"no boot image in {directory}"
    return images[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=int, default=300)
    options = parser.parse_args()

    image = newest_image(BUILD)
    data_source = BUILD / "PollikData-test.img"
    data_image = BUILD / "PollikData-console.img"
    shutil.copyfile(data_source, data_image)

    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()

    process = subprocess.Popen([
        "qemu-system-x86_64", "-accel", "tcg", "-machine", "pc",
        "-cpu", "qemu64", "-m", "64", "-vga", "std", "-nic", "none",
        "-display", "none", "-monitor", "none",
        "-serial", f"tcp:127.0.0.1:{port},server=on,nowait",
        "-no-reboot", "-no-shutdown",
        "-drive", f"file={image},format=raw,if=ide,index=0,snapshot=on",
        "-drive", f"file={data_image},format=raw,if=ide,index=1",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    stream = None
    console = None
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            try:
                stream = socket.create_connection(("127.0.0.1", port), timeout=2)
                break
            except OSError:
                time.sleep(0.2)
        assert stream is not None, "serial socket never accepted a connection"
        console = Console(stream)
        console.wait_for("[X64] ready (Ring 3, native .pol windows, Desktop/Files Dock apps)",
                         timeout=options.timeout)
        console.wait_for("[AUTH64] First run: create a local account.",
                         timeout=options.timeout)
        console.send("x64console")
        console.wait_for("Create password (6-63 characters):", timeout=options.timeout)
        console.send("test123")
        console.wait_for("Confirm password:", timeout=options.timeout)
        console.send("test123")
        console.wait_for("Account created.", timeout=options.timeout)
        print("PASS: first-run account saved to the disposable PollikFS disk")
        console.wait_for("[terminal] shell connected through PollikOS pipes", timeout=options.timeout)
        console.wait_for("PollikOS:/>", timeout=options.timeout)
        console.wait_for_prompt(timeout=options.timeout)
        print("PASS: shell prompt reached")

        console.run("/bin/pipe_nowait.pol", expected="PIPE_NOWAIT_PASS")
        print("PASS: nonblocking pipe read reports empty and returns queued bytes")

        console.run("pwd", expected="\n/\n")
        console.run("cd /home")
        assert "PollikOS:/home>" in console.text(), console.text()[-400:]
        print("PASS: cwd prompt and cd")

        console.run("ls /home", expected="hello.c")
        console.run("ls", expected="hello.c")
        console.run("cat hello.c", expected="int main(void)")
        print("PASS: ls and cat")

        console.run('echo "hello world"', expected="\nhello world\n")
        console.run("echo 'single quoted'", expected="\nsingle quoted\n")
        console.run("echo $PATH", expected="\n/bin\n")
        print("PASS: quoting and variable expansion")

        console.run("touch /home/probe.txt")
        console.run("ls /home", expected="probe.txt")
        console.run("mv /home/probe.txt /home/probe2.txt")
        console.run("ls /home", expected="probe2.txt")
        console.run("mkdir /home/probedir")
        console.run("ls /home", expected="probedir/")
        console.run("rmdir /home/probedir")
        removed = len(console.transcript)
        console.run("ls /home")
        assert "probedir" not in console.text()[removed:], "rmdir did not remove the directory"
        console.run("rm /home/probe2.txt")
        removed = len(console.transcript)
        console.run("ls /home")
        assert "probe2.txt" not in console.text()[removed:], "rm did not remove the file"
        print("PASS: file commands (touch, mv, mkdir, rmdir, rm)")

        console.run("echo first > /home/redir.txt")
        console.run("cat /home/redir.txt", expected="\nfirst\n")
        console.run("echo second >> /home/redir.txt")
        console.run("cat /home/redir.txt", expected="\nfirst\nsecond\n")
        console.run("cat < /home/redir.txt", expected="\nfirst\nsecond\n")
        console.run("rm /home/redir.txt")
        print("PASS: output and input redirection (> and >>, <)")

        console.run("cat < /home/nope", expected="no such file or directory")
        console.run("echo $?", expected="\n1\n")
        print("PASS: redirection errors report failure")

        console.run("cat /home/hello.c | cat | cat", expected="int main(void)")
        console.run("echo piped | cat", expected="\npiped\n")
        console.run("ls /home | cat", expected="hello.c")
        print("PASS: pipelines (|) between builtins and programs")

        console.run("tcc hello.c -o hello.pol", timeout=options.timeout)
        console.run("./hello.pol", expected="Hello from self-hosted PollikOS C!", timeout=180)
        console.run("echo $?", expected="\n42\n")
        print("PASS: native TinyCC compile, link and run")

        console.type_text("echo ab")
        console.type_bytes(b"\x1b[D")
        console.type_bytes(b"X")
        console.enter()
        console.wait_for("\naXb", timeout=30)
        console.wait_for_prompt(timeout=30)

        console.type_text("echo oXbc")     # cursor: after the second X
        console.type_bytes(b"\x1b[D\x1b[D")  # Left over 'c' and 'b'
        console.type_bytes(b"\x1b[3~")     # Delete removes 'b'
        console.enter()
        console.wait_for("\noXc", timeout=30)
        console.wait_for_prompt(timeout=30)

        console.type_text("echo tail")
        console.type_bytes(b"\x1b[H")      # Home
        console.type_bytes(b"\x1b[6~")     # unsupported key is ignored
        console.type_bytes(b"\x1b[F")      # End
        console.type_bytes(b"!")
        console.enter()
        console.wait_for("\ntail!", timeout=30)
        console.wait_for_prompt(timeout=30)
        print("PASS: mid-line editing, delete, home and end")

        console.run("echo one", expected="\none")
        console.run("echo two", expected="\ntwo")
        console.type_bytes(b"\x1b[A")  # Up recalls "echo two"
        console.enter()
        console.wait_for("\ntwo", timeout=30)
        console.wait_for_prompt(timeout=30)
        recall = len(console.transcript)
        console.type_bytes(b"\x1b[A\x1b[A")  # Up twice recalls "echo one"
        console.wait_for("echo one", timeout=30, start=recall)
        console.type_bytes(b"\x03")  # Ctrl+C cancels without touching history
        console.wait_for_prompt(timeout=30)
        console.type_bytes(b"\x1b[A\x1b[A\x1b[B")  # Down returns toward "echo two"
        console.enter()
        console.wait_for("\ntwo", timeout=30)
        console.wait_for_prompt(timeout=30)
        print("PASS: command history recall")

        console.run("history", expected="tcc hello.c -o hello.pol")
        console.run("zzz_not_a_command", expected="is not recognized as a PollikOS command or executable.")
        print("PASS: history and unknown command reporting")

        console.send("spin_c")  # foreground program that never exits
        time.sleep(1.0)
        console.type_bytes(b"\x03")  # Ctrl+C kills the foreground child
        console.wait_for_prompt(timeout=30)
        console.run("echo $?", expected="\n130\n")
        print("PASS: Ctrl+C kills the foreground child")

        console.run("help", expected="clear | cls")
        keep = console.transcript.find(b"PollikOS shell builtins:")
        assert keep >= 0, "help output missing"
        console.run("echo keep_me_visible", expected="\nkeep_me_visible\n")
        console.run("pwd", expected="\n/home\n")
        assert b"keep_me_visible" in console.transcript[keep:], "earlier output disappeared"
        assert b"\x1b[2J" not in console.transcript[keep:], "output was cleared without clear"
        print("PASS: output retained for scrollback")

        start = len(console.transcript)
        console.run("clear")
        assert b"\x1b[2J" in console.transcript[start:], "clear sequence missing"
        print("PASS: clear screen sequence")

        console.run("exit", expected="[TTY] /bin/pollish exited")
        console.wait_for("[TTY] shell exited; restarting", timeout=30)
        console.wait_for("PollikOS:/>", timeout=options.timeout)
        console.wait_for_prompt(timeout=options.timeout)
        console.run("history", expected="tcc hello.c -o hello.pol")
        print("PASS: exit, relaunch and persistent history")

        # Reboot QEMU against the same disposable data disk. This covers the
        # persisted account record rather than only the in-memory first run.
        stream.close()
        stream = None
        process.terminate()
        process.communicate(timeout=5)
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        process = subprocess.Popen([
            "qemu-system-x86_64", "-accel", "tcg", "-machine", "pc",
            "-cpu", "qemu64", "-m", "64", "-vga", "std", "-nic", "none",
            "-display", "none", "-monitor", "none",
            "-serial", f"tcp:127.0.0.1:{port},server=on,nowait",
            "-no-reboot", "-no-shutdown",
            "-drive", f"file={image},format=raw,if=ide,index=0,snapshot=on",
            "-drive", f"file={data_image},format=raw,if=ide,index=1",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            try:
                stream = socket.create_connection(("127.0.0.1", port), timeout=2)
                break
            except OSError:
                time.sleep(0.2)
        assert stream is not None, "serial socket did not return after reboot"
        console = Console(stream)
        console.wait_for("[AUTH64] Sign in to PollikOS.", timeout=options.timeout)
        console.send("x64console")
        console.wait_for("Password:", timeout=options.timeout)
        console.send("bad-password")
        console.wait_for("Sign-in failed.", timeout=options.timeout)
        console.wait_for("Username:", timeout=options.timeout)
        console.send("x64console")
        console.wait_for("Password:", timeout=options.timeout)
        console.send("test123")
        console.wait_for("[AUTH64] Sign-in successful.", timeout=options.timeout)
        console.wait_for("PollikOS shell (pollish).", timeout=options.timeout)
        console.wait_for_prompt(timeout=options.timeout)
        console.run("ls /home", expected="x64console")
        print("PASS: persisted account rejects a wrong password and logs in after reboot")

        text = console.text()
        assert "PANIC" not in text and "[X64] FAIL" not in text, "kernel failure in transcript"
        (BUILD / "console.log").write_text(text, encoding="utf-8", errors="replace")
    finally:
        if console is not None:
            (BUILD / "console.log").write_text(console.text(), encoding="utf-8", errors="replace")
        if stream is not None:
            stream.close()
        process.terminate()
        try:
            process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
    print("CONSOLE PASS")


if __name__ == "__main__":
    sys.exit(main())
