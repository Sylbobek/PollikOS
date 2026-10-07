"""Live-site investigation: real guest transport, separate script failures.

This is not a standards-compliance test. Each URL gets a fresh disposable disk.
No localhost proxy or host renderer is used by the guest browser.
"""
import argparse
import json
import pathlib
import re
import socket
import subprocess
import tempfile
import time
from PIL import Image
from gui_fixture import create_gui_disk, finish_setup

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'

def investigate(url, index, options):
    with tempfile.TemporaryDirectory(prefix='pollik-browser-site-') as directory:
        data = pathlib.Path(directory) / 'data.img'
        create_gui_disk(data)
        log = BUILD / f'browser-site-{index}.log'
        log.write_text('')
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0))
            port = reserve.getsockname()[1]
        command = ['qemu-system-x86_64', '-machine', 'pc', '-accel', options.accel,
            '-cpu', options.cpu, '-rtc', 'base=utc', '-m', '2G', '-vga', 'std',
            '-drive', f'file={BUILD / "PollikOS-Alpha.img"},format=raw,if=ide,index=0,snapshot=on',
            '-drive', f'file={data},format=raw,if=ide,index=1,snapshot=on',
            '-netdev', 'user,id=n', '-device', 'rtl8139,netdev=n', '-display', 'none',
            '-serial', f'file:{log}', '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off']
        print('COMMAND ' + subprocess.list2cmdline(command), flush=True)
        process = subprocess.Popen(command, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        connection = stream = None
        try:
            end = time.monotonic() + 30
            while connection is None:
                try: connection = socket.create_connection(('127.0.0.1', port), timeout=2)
                except OSError:
                    assert time.monotonic() < end, 'QMP startup timed out'
                    time.sleep(.1)
            stream = connection.makefile('rwb', buffering=0)
            stream.readline()
            def qmp(name, args=None):
                stream.write((json.dumps({'execute': name, 'arguments': args or {}}) + '\n').encode())
                while True:
                    response = json.loads(stream.readline())
                    assert 'error' not in response, response
                    if 'return' in response: return response['return']
            def key(name):
                qmp('human-monitor-command', {'command-line': 'sendkey ' + name + ' 1'})
                time.sleep(.12)
            qmp('qmp_capabilities')
            while 'desktop ready' not in log.read_text():
                assert time.monotonic() < end, 'desktop startup timed out'
                time.sleep(.1)
            finish_setup(qmp, log)
            key('f6')
            time.sleep(1)
            key('ctrl-l')
            for ch in url:
                key({'.':'dot', ':':'shift-semicolon', '/':'slash', '?':'shift-slash',
                     '=':'equal', '-':'minus', '_':'shift-minus'}.get(ch, ch))
            mark = len(log.read_text())
            key('ret')
            end = time.monotonic() + options.timeout
            while True:
                text = log.read_text()[mark:]
                if 'BROWSER: Page loaded successfully' in text or any(message in text for message in (
                    'HTTP: TLS handshake failed', 'HTTP: TCP connect failed',
                    'HTTP: DNS failed', 'HTTP: Empty response', 'KERNEL SYSTEM HALTED')):
                    break
                if process.poll() is not None or time.monotonic() >= end: break
                time.sleep(.2)
            loaded = 'BROWSER: Page loaded successfully' in text
            statuses = re.findall(r'HTTP: Response status: (\d+)', text)
            errors = [line for line in text.splitlines() if line.startswith('JS: ERROR')]
            panic = 'KERNEL PANIC' in text
            heap_errors = text.count('MEM kfree corrupt block')
            print(f'RESULT url={url} loaded={loaded} statuses={statuses} JS_errors={len(errors)} '
                  f'panic={panic} heap_errors={heap_errors}', flush=True)
            for line in text.splitlines():
                if any(tag in line for tag in ('HTTP:', 'TLS:', 'BROWSER title:', 'JS: ERROR',
                                              'MEM kfree', 'KERNEL PANIC', 'eip=', 'reason=')):
                    print(line, flush=True)
            if panic:
                stack = re.search(r'ESP: (0x[0-9a-f]+)', text)
                if stack:
                    dump = qmp('human-monitor-command', {'command-line': 'xp /80wx ' + stack[1]})
                    (BUILD / f'browser-site-{index}-stack.txt').write_text(dump)
            shot = BUILD / f'browser-site-{index}.ppm'
            qmp('screendump', {'filename': str(shot)})
            Image.open(shot).save(shot.with_suffix('.png'))
            return loaded and bool(statuses) and statuses[0] == '200' and not panic and not heap_errors
        finally:
            process.terminate()
            process.wait(timeout=5)
            if stream: stream.close()
            if connection: connection.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--accel', choices=('tcg', 'whpx'), default='tcg')
    parser.add_argument('--cpu', default='max')
    parser.add_argument('--timeout', type=int, default=90)
    parser.add_argument('urls', nargs='*', default=['https://www.wikipedia.org',
        'https://en.m.wikipedia.org', 'http://neverssl.com', 'https://www.google.com',
        'https://www.youtube.com', 'http://frogfind.com'])
    options = parser.parse_args()
    outcomes = [investigate(url, index, options) for index, url in enumerate(options.urls)]
    print(f'LIVE_SITE_RESULTS loaded_200={sum(outcomes)}/{len(outcomes)} (not JS/CSS conformance)', flush=True)
    raise SystemExit(0 if all(outcomes) else 1)
