"""Preserve stdout/stderr and propagate the actual native command exit code."""
import argparse
from pathlib import Path
import subprocess
import sys

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--log', required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == '--': command = command[1:]
    if not command: parser.error('command is required')
    if command[0].endswith('.ps1'):
        command = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', *command]
    log = Path(args.log)
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open('wb') as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    lines = log.read_text(encoding='utf-8', errors='replace').splitlines()
    print('\n'.join(lines[-4:] if result.returncode == 0 else lines[-40:]))
    return result.returncode

if __name__ == '__main__':
    sys.exit(main())
