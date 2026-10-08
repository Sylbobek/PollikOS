"""Real PS/2 file moves and independent viewers on a disposable QEMU disk."""
import struct
import tempfile
import time
from pathlib import Path
from PIL import Image
from gui_metrics import Guest, BUILD
from login_gui import create_login_disk
from gui_fixture import PollikFsImage

PAYLOAD = b'Native file drag test\nThe original contents must survive every move.\n'


def main():
    with tempfile.TemporaryDirectory(prefix='pollik-file-drag-') as folder:
        disk = Path(folder) / 'data.img'
        create_login_disk(disk, True)
        fs = PollikFsImage.load(disk)
        fs.ensure_directory('/home/Target')
        for name, data in (('move-me.txt', PAYLOAD), ('folder-file.txt', PAYLOAD), ('conflict.txt', b'source')):
            fs.install_file('/home/' + name, data)
        fs.install_file('/home/Target/conflict.txt', b'destination')
        disk.write_bytes(fs.data)
        with Guest('1024x768', 'files-drag-viewers', data_image=disk, boot_only=True,
                   headless=True, boot_timeout=120, allow_reboot=True) as g:
            def scalar(name):
                return g.words(name)[0]

            def key(name):
                g.hmp('sendkey ' + name + ' 1')
                time.sleep(.09)

            def click(x, y):
                g.move(x, y)
                g.button(True)
                g.button(False)

            def entries():
                count = scalar('g_files_count')
                data = g.memory(*g.symbol('g_files_entries'))
                return [data[i * 72:i * 72 + 64].split(b'\0')[0].decode() for i in range(count)]

            def settle():
                # An acknowledged PS/2 movement is serviced by the GUI owner
                # after its directory callback; idle frames need not advance.
                px, py = scalar('mx'), scalar('my')
                g.move(px + (1 if px < 1022 else -1), py)

            def files():
                key('f2')
                g.wait(lambda: scalar('g_focused_window') == 1, 'Files focused')
                g.wait(lambda: not g.words('g_window_anims', 1)[0], 'Files settled')
                return g.window(1)

            def place(index):
                w = files()
                click(w[2] + 34, w[3] + 108 + index * 32)
                expected = ('/home', '/home/Desktop', '/home/Desktop/Documents', '/home/Trash', '/Applications')[index]
                g.wait(lambda: g.memory(*g.symbol('g_files_current_path')).split(b'\0')[0].decode() == expected,
                       'folder navigation')
                settle()
                return w

            def select(name):
                g.wait(lambda: name in entries(), 'directory lists ' + name)
                settle()
                names = entries()
                assert name in names, (name, names)
                index = names.index(name)
                # A fresh sidebar navigation resets selection. Keyboard input
                # scrolls the real list until the requested row is visible.
                selected = scalar('g_files_selected')
                if selected == 0xffffffff:
                    key('down')
                    g.wait(lambda: scalar('g_files_selected') == 0, 'first selection')
                    selected = 0
                while selected != index:
                    following = selected + (1 if selected < index else -1)
                    key('down' if selected < index else 'up')
                    g.wait(lambda: scalar('g_files_selected') == following, 'selection advances')
                    selected = following
                w = g.window(1)
                row = index - scalar('first_row')
                return w[2] + (156 if w[4] < 600 else 196) + 54, w[3] + 136 + row * 40 + 16

            def drag(name, target):
                start = select(name)
                g.move(*start)
                g.button(True)
                g.wait(lambda: scalar('g_drag_external') == 1, 'Files drag arms')
                g.move(*target)
                g.wait(lambda: scalar('g_drag_moved') == 1, 'drag crosses movement threshold')
                g.button(False)
                g.wait(lambda: not scalar('g_drag_active'), 'drop completes')

            def open_entry(name, app):
                select(name)
                key('ret')
                g.wait(lambda: scalar('g_focused_window') == app and g.window(app)[7], name + ' opens its viewer')
                g.wait(lambda: not g.words('g_window_anims', app)[0], 'viewer settled')

            def doc_bytes():
                return g.memory(scalar('document_data'), scalar('document_size'))

            def screenshot(app, name):
                w = g.window(app)
                surface = g.words('g_surfaces', app)
                lx, ly = (240, 146) if app == 1 else (30, 65)
                tw, th = 100, 18
                shot = BUILD / ('files-redesign-' + name + '.ppm')
                deadline = time.monotonic() + 30
                while True:
                    # A newly opened document can still have the previous
                    # document's caption cached until its first paint.
                    block = g.memory(surface[0] + (ly * surface[3] + lx) * 4,
                                     ((th - 1) * surface[3] + tw) * 4)
                    raw = b''.join(block[row * surface[3] * 4:row * surface[3] * 4 + tw * 4]
                                   for row in range(th))
                    expected = Image.frombytes('RGB', (tw, th), raw, 'raw', 'BGRX')
                    colors = expected.tobytes()
                    g.screendump(shot)
                    picture = Image.open(shot)
                    actual = picture.crop((w[2] + lx, w[3] + ly, w[2] + lx + tw, w[3] + ly + th))
                    if len({colors[i:i+3] for i in range(0,len(colors),3)}) > 1 and actual.tobytes() == colors:
                        picture.save(shot.with_suffix('.png'))
                        return
                    assert time.monotonic() < deadline, (name, 'caption has not been presented')
                    time.sleep(.12)

            g.wait(lambda: '[TEST] PHASE 2 PASS' in g.log.read_text(), 'boot stress', 120)
            for c in 'test123':
                key(c)
            key('ret')
            g.wait(lambda: 'AUTH: login accepted' in g.log.read_text(), 'login', 45)
            place(0)
            drag('move-me.txt', (974, 384))
            assert 'move-me.txt' not in entries(), 'source still exists after desktop drop'
            place(1)
            open_entry('move-me.txt', 10)
            assert doc_bytes() == PAYLOAD
            print('PASS Files -> desktop: native move, same contents, Documents window', flush=True)
            place(0)
            names = entries()
            w = g.window(1)
            target = names.index('Target')
            target_point = (w[2] + 196 + 60, w[3] + 136 + target * 40 + 16)
            drag('folder-file.txt', target_point)
            assert 'folder-file.txt' not in entries()
            place(0)
            select('Target')
            key('ret')
            g.wait(lambda: g.memory(*g.symbol('g_files_current_path')).split(b'\0')[0] == b'/home/Target', 'Target opens')
            open_entry('folder-file.txt', 10)
            assert doc_bytes() == PAYLOAD
            print('PASS Files -> folder: same contents and original directory updated', flush=True)
            place(0)
            drag('conflict.txt', target_point)
            assert 'conflict.txt' in entries(), 'collision overwrote the source'
            place(0)
            select('Target')
            key('ret')
            open_entry('conflict.txt', 10)
            assert doc_bytes() == b'destination', 'collision overwrote destination contents'
            print('PASS same-name collision preserves source and destination', flush=True)
            place(1)
            open_entry('photo.png', 8)
            place(1)
            open_entry('film.pkv', 9)
            assert g.window(8)[7] and g.window(9)[7] and g.window(10)[7]
            photo = struct.unpack_from('<7I', g.memory(*g.symbol('photo_view')))
            video = struct.unpack_from('<7I', g.memory(*g.symbol('video_view')))
            assert photo[0] and photo[4:6] == (256, 160) and video[3] and video[4] > 0
            key('spc')
            g.wait(lambda: struct.unpack_from('<7I', g.memory(*g.symbol('video_view')))[6] == 0, 'space pauses video')
            key('spc')
            g.wait(lambda: struct.unpack_from('<7I', g.memory(*g.symbol('video_view')))[6] == 1, 'space resumes video')
            print('PASS Photos, Video and Documents coexist; video has native play/pause', flush=True)
            for app, name in ((1, 'files'), (8, 'photos'), (9, 'video'), (10, 'documents')):
                if app == 10:
                    place(1)
                    open_entry('move-me.txt', 10)
                else:
                    key({1: 'f2', 8: 'f9', 9: 'f10'}[app])
                g.wait(lambda: scalar('g_focused_window') == app, name + ' focused')
                screenshot(app, name)
            # The snapshot overlay stays attached across a real BIOS reboot.
            previous = g.log.read_text().count('desktop ready')
            passes = g.log.read_text().count('[TEST] PHASE 2 PASS')
            g.qmp('system_reset')
            g.wait(lambda: g.log.read_text().count('desktop ready') > previous, 'reboot', 120)
            g.wait(lambda: g.log.read_text().count('[TEST] PHASE 2 PASS') > passes, 'reboot stress', 120)
            previous = g.log.read_text().count('AUTH: login accepted')
            for c in 'test123':
                key(c)
            key('ret')
            g.wait(lambda: g.log.read_text().count('AUTH: login accepted') > previous, 'persisted account login', 45)
            place(1)
            open_entry('move-me.txt', 10)
            assert doc_bytes() == PAYLOAD
            place(0)
            select('Target')
            key('ret')
            open_entry('folder-file.txt', 10)
            assert doc_bytes() == PAYLOAD
            assert 'PANIC' not in g.log.read_text()
            print('PASS reboot persists both file moves and contents', flush=True)


if __name__ == '__main__':
    main()
