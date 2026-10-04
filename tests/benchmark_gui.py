"""Verified AFTER benchmark: five windows, held drag/resize, 20 cycles, dock.

No synthetic before/after score: historical BEFORE is NOT VERIFIED and the
DIFFERENCE is not comparable. Run both resolutions by default after building.
"""
import argparse
import json
import time

from gui_metrics import BUILD, Guest, stage_result


def benchmark(resolution):
    report = dict(status='RUNNING', stages=[], historical_before='NOT VERIFIED',
                  difference='not comparable: old instrumentation/scenario were invalid')
    path = BUILD / f'benchmark-{resolution}.json'
    started = time.monotonic()
    try:
        with Guest(resolution) as g:
            report['environment'] = g.environment
            report['serial_log'] = str(g.log)
            def finish(name, before, t0, operations):
                result = stage_result(name, before, g.capture(), t0, operations)
                report['stages'].append(result)
                path.write_text(json.dumps(report, indent=2))
                print(f'{resolution} {name}: {operations} verified operations; '
                      f'{result["actual_fps"]:.2f} actual fps; '
                      f'{result["render_throughput_fps"]:.2f} render/s', flush=True)
                phases = result['phase_us']
                print(f'{resolution} {name} detail: dirty={result["mean_composed_pixels_per_frame"]:.0f} px/frame; '
                      f'phase_us input/app/layout/draw/compose/LFB='
                      f'{phases["input"]}/{phases["app_update"]}/{phases["layout"]}/'
                      f'{phases["draw"]}/{phases["composition"]}/{phases["present"]}; '
                      f'frame mean/p95/max={result["frame_history"]["mean_us"]:.0f}/'
                      f'{result["frame_history"]["p95_us"]}/{result["frame_history"]["max_us"]} us; '
                      f'interval mean/p95/max={result["interval_history"]["mean_us"]:.0f}/'
                      f'{result["interval_history"]["p95_us"]}/{result["interval_history"]["max_us"]} us', flush=True)
                return result

            before, t0 = g.capture(), time.monotonic()
            for app in (1, 3, 4, 0, 2):
                g.key(f'f{app + 1}', lambda app=app: g.words('g_focused_window')[0] == app,
                      f'app {app} focus')
                g.wait(lambda app=app: g.presented(app), f'app {app} not presented')
            visible = [i for i in range(g.apps) if g.window(i)[7] and not g.window(i)[8] and g.window(i)[10]]
            assert visible == [0, 1, 2, 3, 4], visible
            # Establish five-window baseline while moving only the cursor for 10s.
            hold = time.monotonic()
            steps = 0
            while time.monotonic() - hold < 10:
                g.move(g.width - 45 + 10 * (steps % 2), 65)
                steps += 1
            finish('five_windows_10s', before, t0, steps)

            # Each held motion uses current geometry, confirms capture and waits
            # for actual consumption before sending another relative packet.
            for mode in ('drag', 'resize'):
                w = g.window()
                x, y = (w[2] + 220, w[3] + 17) if mode == 'drag' else (w[2] + w[4] - 1, w[3] + w[5] - 1)
                g.move(x, y)
                g.button(True)
                capture = 'g_dragged_window' if mode == 'drag' else 'g_resized_window'
                if mode == 'drag':
                    g.wait(lambda: g.words('shell')[8:10] == (2, 1), 'pending drag did not engage')
                    g.move(x + 6, y)
                g.wait(lambda: g.words(capture)[0] == 2, f'{mode} capture did not engage')
                before, t0 = g.capture(), time.monotonic()
                steps = 0
                while time.monotonic() - t0 < 10:
                    dx, dy = (12, 6) if steps % 2 == 0 else (0, 0)
                    g.move(x + dx, y + dy)
                    expected = (w[2] + dx, w[3] + dy, w[4], w[5]) if mode == 'drag' else (w[2], w[3], w[4] + dx, w[5] + dy)
                    g.wait(lambda: g.window()[2:6] == expected, f'{mode} geometry did not follow pointer')
                    assert g.words(capture)[0] == 2 and g.words('pointer_packet.held')[0] == 1
                    g.wait(lambda: g.presented(), f'{mode} frame did not reach LFB')
                    steps += 1
                g.button(False)
                g.wait(lambda: g.words(capture)[0] == 0xffffffff, f'{mode} capture did not release')
                assert steps >= 10, f'{mode} insufficient verified progress: {steps}'
                result = finish(f'{mode}_held_10s', before, t0, steps)
                assert result['duration_host_s'] >= 10
                if mode == 'resize':
                    assert result['counts']['client_paint_count'] > 0

            before, t0 = g.capture(), time.monotonic()
            restored = g.window()[2:6]
            for cycle in range(20):
                for state in (2, 0):
                    # Click the REAL green titlebar control at CURRENT geometry.
                    w = g.window()
                    g.move(w[2] + 54, w[3] + 17)
                    g.button(True)
                    g.wait(lambda: g.window()[6] == state, f'cycle {cycle}: state {state} not reached')
                    g.button(False)
                    g.wait(lambda: g.presented(), f'cycle {cycle}: frame not presented')
                    expected = (0, 32, g.width, g.height - 128) if state == 2 else restored
                    assert g.window()[2:6] == expected, (cycle, state, g.window())
            finish('maximize_restore_20_cycles', before, t0, 20)

            # Match desktop.c dock_hit geometry, including even app counts.
            center = (g.apps - 1) // 2
            first_x = g.width // 2 - (g.apps - 1) * 34
            dock_y = g.height - 60
            hits = {}

            def hover(index):
                x = first_x + index * 68
                assert 0 <= index < g.apps
                g.move(x, dock_y)
                g.wait(lambda: g.words('shell')[-g.apps - 1] == index,
                       f'dock hover {index} did not engage at {(x, dock_y)}')
                observed = g.words('shell')[-g.apps - 1]
                assert g.pointer() == (x, dock_y) and observed == index
                hits[index] = dict(pointer=[x, dock_y], expected=index, observed=observed)

            hover(center)
            before, t0 = g.capture(), time.monotonic()
            steps = 0
            while time.monotonic() - t0 < 3:
                # Cross distinct icon hit regions (±30 remains in one icon).
                hover(center + (1 if steps % 2 else -1))
                steps += 1
            result = finish('dock_hover_3s', before, t0, steps)
            result['dock_hit_proof'] = dict(apps=g.apps, center_index=center,
                                            hits=list(hits.values()))
            assert result['counts']['dock_frames'] > 0, 'dock path not accounted'
            assert result['duration_host_s'] >= 3 and steps >= 3

            # PollikMark is still closed, so its F7 launch exercises the real
            # window-open animation without entering the benchmark workload.
            before, t0 = g.capture(), time.monotonic()
            g.key('f7', lambda: g.window(6)[7] == 1 and g.words('g_window_anims', 6)[0] == 1,
                  'PollikMark open animation did not start')
            g.wait(lambda: g.words('g_window_anims', 6)[0] == 0,
                   'PollikMark open animation did not finish', 3)
            g.wait(lambda: g.presented(6), 'PollikMark open animation final surface not presented')
            finish('window_open_animation', before, t0, 1)
            report['perf_summary'] = g.perf_command()
            old_panel = g.backbuffer_pixel(9, 36)
            assert old_panel != 0x161420, f'overlay test point already has panel color: {old_panel:#x}'
            g.key('f12', lambda: g.words('g_perf_overlay_enabled')[0] == 1,
                  'F12 did not enable performance overlay')
            g.wait(lambda: g.backbuffer_pixel(9, 36) == 0x161420,
                   'enabled performance overlay did not paint its panel')
            g.key('f12', lambda: g.words('g_perf_overlay_enabled')[0] == 0,
                  'F12 did not disable performance overlay')
            g.wait(lambda: g.backbuffer_pixel(9, 36) != 0x161420,
                   'disabled performance overlay did not restore the scene')
            print(f'{resolution} [PERF OVERLAY PASS] F12 on/off; backbuffer panel pixel restored', flush=True)
            assert not any(x in g.log.read_text() for x in ('GUI MEMORY CORRUPTION', 'KERNEL PANIC'))
            report['status'] = 'PASS'
    except BaseException as error:
        report['status'] = 'FAIL'
        report['error'] = repr(error)
        raise
    finally:
        report['duration_host_s'] = time.monotonic() - started
        path.write_text(json.dumps(report, indent=2))
        print(path, flush=True)
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--resolution', action='append')
    args = parser.parse_args()
    for resolution in args.resolution or ['1024x768', '1920x1080']:
        benchmark(resolution)


if __name__ == '__main__':
    main()
