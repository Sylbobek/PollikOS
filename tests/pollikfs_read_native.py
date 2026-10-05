"""Run the actual shared PollikFS reader against synthetic sector storage."""
import argparse
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--expect-cache', action='store_true')
args = parser.parse_args()
cmd = ['clang', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fuse-ld=lld',
       'tests/pollikfs_read_native.c', '-o', 'build/pollikfs_read_native.exe']
if args.expect_cache: cmd.append('-DEXPECT_CACHED_READS=1')
print('COMMAND ' + subprocess.list2cmdline(cmd), flush=True)
subprocess.run(cmd, cwd=ROOT, check=True)
subprocess.run([str(ROOT/'build/pollikfs_read_native.exe')], cwd=ROOT, check=True)
