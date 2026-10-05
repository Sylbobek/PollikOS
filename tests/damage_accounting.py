"""Focused GUI damage-accounting probe using the shared disposable guest."""
import argparse
import time

from gui_metrics import COUNTERS, Guest


def bar_time(g):
    address, size = g.symbol('g_bar_time')
    return bytes(g.memory(address, size)).split(b'\0', 1)[0].decode('ascii')


def report(g, name, before, seconds):
    after = g.capture()
    delta = {key: after[key] - before[key] for key in COUNTERS}
    print(f'DAMAGE res={g.resolution} stage={name} duration_s={seconds:.2f} '
          f'frames={delta["frame_count"]} full={delta["full_redraw_count"]} '
          f'partial={delta["partial_frames"]} dock={delta["dock_frames"]} '
          f'rects={delta["damage_rects_count"]} '
          f'composed_px={delta["composed_pixels_total"]} '
          f'lfb_px={delta["presented_pixels_total"]}', flush=True)
    return after, delta


def animations_active(g):
    # Inactive records retain their last geometry; only word zero is `active`.
    return any(g.words('g_window_anims', app)[0] for app in range(g.apps))


def run(resolution, skip_clock=False):
    with Guest(resolution, label='damage-accounting', headless=True, accel='tcg', cpu='qemu64') as g:
        g.wait(lambda: not animations_active(g), 'initial animations did not settle', 10)
        time.sleep(2)  # Let one-shot first desktop/menu initialization finish.
        # Capture a completed RTC minute transition, then sample quiet intervals
        # immediately after it to avoid attributing that one intentional frame
        # to idle or the Notes caret.
        if not skip_clock:
            old_time = bar_time(g)
            before = g.capture()
            clock_start = time.monotonic()
            deadline = clock_start + 70
            while bar_time(g) == old_time and time.monotonic() < deadline:
                time.sleep(.25)
            assert bar_time(g) != old_time, 'desktop clock did not advance within 70 seconds'
            time.sleep(.25)
            _, clock_delta = report(g, 'clock_minute_tick', before, time.monotonic() - clock_start)
            print(f'DAMAGE_CLOCK res={resolution} old={old_time} new={bar_time(g)} '
                  f'partial_frames={clock_delta["partial_frames"]} '
                  f'full_redraws={clock_delta["full_redraw_count"]}', flush=True)
            assert clock_delta['frame_count'] >= 1 and clock_delta['partial_frames'] >= 1
        else:
            print(f'DAMAGE_CLOCK res={resolution} NOT_RUN (--skip-clock)', flush=True)

        before = g.capture()
        start = time.monotonic()
        time.sleep(4)
        after, idle_delta = report(g, 'idle_no_input_4s', before, time.monotonic() - start)
        assert idle_delta['frame_count'] == 0 and idle_delta['presented_pixels_total'] == 0, idle_delta

        g.key('f4', lambda: g.window(3)[7] == 1, 'Notes did not open')
        g.wait(lambda: g.words('g_window_anims', 3)[0] == 1,
               'Notes opening animation was not observed', 5)
        g.wait(lambda: g.words('g_window_anims', 3)[0] == 0, 'Notes animation did not finish', 5)
        g.wait(lambda: not animations_active(g), 'other window animations did not finish', 5)
        g.wait(lambda: g.words('g_dock_launch_app')[0] == 0xffffffff,
               'Dock launch animation did not finish', 5)
        g.wait(lambda: g.presented(3), 'Notes surface was not presented', 5)
        note = g.window(3)
        caret_row = g.words('note_caret_row')[0]
        caret_x = g.words('note_caret_x')[0]
        caret_pos = (note[2] + 64 + caret_x, note[3] + 93 + caret_row * 20)
        caret_before = g.backbuffer_pixel(*caret_pos)
        assert caret_before in (0x60a5fa, 0x9470bd), (
            f'Notes caret missing at {caret_pos} row={caret_row} x={caret_x}: {caret_before:#x}')
        note_time = bar_time(g)
        active = [app for app in range(g.apps) if g.words('g_window_anims', app)[0]]
        print(f'DAMAGE_NOTES_STATE res={resolution} overlay={g.words("g_perf_overlay_enabled")[0]} '
              f'active_animations={active} pointer={g.pointer()}', flush=True)
        before = g.capture()
        start = time.monotonic()
        time.sleep(3)
        after, caret_delta = report(g, 'notes_static_caret_3s', before, time.monotonic() - start)
        caret_after = g.backbuffer_pixel(*caret_pos)
        print(f'DAMAGE_CARET res={resolution} pixel={caret_pos} row={caret_row} x={caret_x} '
              f'pixel_before={caret_before:#08x} '
              f'pixel_after={caret_after:#08x} clock_before={note_time} clock_after={bar_time(g)}', flush=True)
        assert caret_after == caret_before
        assert caret_delta['frame_count'] == 0 and caret_delta['presented_pixels_total'] == 0, caret_delta

        apps = g.apps
        center = (apps - 1) // 2
        first_x = g.width // 2 - (apps - 1) * 34
        dock_y = g.height - 60

        def hover(index):
            x = first_x + index * 68
            g.move(x, dock_y)
            g.wait(lambda: g.words('shell')[-apps - 1] == index,
                   f'dock hover {index} did not engage')

        hover(center)
        before = g.capture()
        start = time.monotonic()
        for index in (center - 1, center + 1, center - 1, center + 1, center):
            hover(index)
            time.sleep(.22)
        after, dock_delta = report(g, 'dock_hover', before, time.monotonic() - start)
        assert dock_delta['dock_frames'] > 0 and dock_delta['full_redraw_count'] == 0, dock_delta
        assert 0 < dock_delta['presented_pixels_total'] < dock_delta['frame_count'] * g.width * g.height, dock_delta


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--resolution', action='append')
    parser.add_argument('--skip-clock', action='store_true', help='reuse a separately recorded RTC rollover')
    args = parser.parse_args()
    for resolution in args.resolution or ('1024x768', '1920x1080'):
        run(resolution, args.skip_clock)
    print('PASS damage accounting: zero-work idle, Notes caret, Dock hover'
          + ('; RTC minute NOT RUN' if args.skip_clock else '; RTC minute'), flush=True)


if __name__ == '__main__':
    main()
