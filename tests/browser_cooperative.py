"""Native real browser/parser/CSS/layout/client/wait-service regression; mocked I/O."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    clang = shutil.which('clang')
    if not clang:
        raise RuntimeError('clang not found')
    executable = ROOT / 'build' / ('browser_cooperative.exe' if os.name == 'nt' else 'browser_cooperative')
    executable.parent.mkdir(exist_ok=True)
    command = [clang, '-std=c11', '-O0', '-g', '-fno-builtin', '-Wall', '-Wextra', '-Werror']
    if os.name == 'nt':
        command.append('-fuse-ld=lld')
    command += ['tests/browser_cooperative.c', 'kernel/browser/browser_app.c',
                'kernel/browser/html_parser.c', 'kernel/browser/css_engine.c',
                'kernel/browser/layout.c', 'kernel/gui/browser_client.c',
                'kernel/net/net_util.c', 'kernel/gui/app_edit.c', '-o', str(executable)]
    print('Compile: ' + subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([str(executable)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
