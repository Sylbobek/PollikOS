"""Real QEMU display switching, window restore and PS/2 context menu checks."""
import json, os, pathlib, socket, subprocess, time

root = pathlib.Path(__file__).resolve().parents[1]
build = root / 'build'
resolution = os.environ.get('POLLIK_TEST_RESOLUTION', '1920x1080')
with socket.socket() as reserve:
    reserve.bind(('127.0.0.1', 0))
    port = reserve.getsockname()[1]
log = build / 'display-test.log'
log.write_text('')
data = build / 'display-test-data.img'
data.write_bytes(bytes(4 * 1024 * 1024))
process = subprocess.Popen([
    'qemu-system-x86_64', '-machine', 'pc', '-cpu', 'max', '-m', '2G',
    '-device', 'VGA,vgamem_mb=32', '-display', 'none',
    '-fw_cfg', f'name=opt/pollikos/display,string={resolution}',
    '-drive', f"format=raw,file={build / os.environ.get('POLLIK_TEST_IMAGE', 'PollikOS-Display.img')},if=ide,index=0",
    '-drive', f'format=raw,file={data},if=ide,index=1',
    '-netdev', 'user,id=n', '-device', 'rtl8139,netdev=n',
    '-serial', f'file:{log}', '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off',
], creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
try:
    deadline = time.monotonic() + 20
    while True:
        try:
            connection = socket.create_connection(('127.0.0.1', port), timeout=2)
            break
        except OSError:
            assert time.monotonic() < deadline
            time.sleep(.1)
    stream = connection.makefile('rwb', buffering=0)
    stream.readline()
    def qmp(command, args=None):
        stream.write((json.dumps({'execute':command,'arguments':args or {}})+'\n').encode())
        while True:
            result = json.loads(stream.readline())
            if 'error' in result: raise AssertionError(result)
            if 'return' in result: return result['return']
    def hmp(command):
        return qmp('human-monitor-command', {'command-line':command})
    def shot(name):
        time.sleep(1.5)
        path = build / (name+'.ppm')
        qmp('screendump', {'filename':str(path)})
        return path.read_bytes()
    qmp('qmp_capabilities')
    while 'desktop ready' not in log.read_text():
        assert time.monotonic()<deadline, log.read_text()
        time.sleep(.1)
    desktop = shot('display-desktop')
    assert resolution.replace('x',' ').encode() in desktop[:50]
    assert 'GFX resolution: '+resolution in log.read_text()
    hmp('sendkey f6'); time.sleep(3)
    # Initial pointer (760,500), green button (234,146).
    for dx,dy in [(-200,-150),(-200,-150),(-126,-54)]:
        hmp(f'mouse_move {dx} {dy}'); time.sleep(.5)
    hmp('mouse_button 1'); time.sleep(.15); hmp('mouse_button 0')
    maximized = shot('display-maximized')
    assert 'WINDOW maximized' in log.read_text(), log.read_text()
    # Green button is now (72,57).
    hmp('mouse_move -162 -89'); time.sleep(.15)
    hmp('mouse_button 1'); time.sleep(.15); hmp('mouse_button 0')
    restored = shot('display-restored')
    assert 'WINDOW restored' in log.read_text(), log.read_text()
    assert maximized != restored
    # Move from (72,57) to the restored browser's bottom-right resize grip.
    for dx,dy in [(200,150),(200,150),(200,150),(174,24)]:
        hmp(f'mouse_move {dx} {dy}');time.sleep(.3)
    hmp('mouse_button 1');time.sleep(.2)
    hmp('mouse_move 180 90');time.sleep(.4)
    hmp('mouse_button 0')
    grown=shot('display-grown')
    assert 'WINDOW resized' in log.read_text(),log.read_text()
    assert grown!=restored
    # Remain inside the new grip and shrink back to the minimum dimensions.
    hmp('mouse_button 1');time.sleep(.2)
    hmp('mouse_move -180 -90');time.sleep(.4)
    hmp('mouse_button 0')
    shrunk=shot('display-shrunk')
    assert log.read_text().count('WINDOW resized')==2,log.read_text()
    assert shrunk!=grown
    hmp('mouse_button 2'); time.sleep(.15); hmp('mouse_button 0')
    menu = shot('display-menu')
    assert 'MENU opened' in log.read_text(), log.read_text()
    assert menu != restored
    print('PASS: '+resolution+', maximize/restore, drag grow/shrink, right-click menu')
finally:
    process.terminate()
    process.wait(timeout=5)
