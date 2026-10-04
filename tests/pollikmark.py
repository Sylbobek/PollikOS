"""Real QEMU, verified ELF image, PS/2-only actions and read-only probes.
No synthetic guest calls/memory writes; retain screenshot and raw results.
"""
import argparse
import json
import struct
from gui_metrics import Guest, BUILD, distribution


def run(resolution, full_run=False):
    with Guest(resolution, 'pollikmark') as g:
        assert g.apps == 7
        scalar = lambda name: g.words(name)[0]
        def guards():
            bases = g.words('g_surface_phys')
            capacities = g.words('g_surface_capacity')
            for base, capacity in zip(bases, capacities):
                assert struct.unpack('<I', g.memory(base, 4))[0] == 0xDEADBEEF
                assert struct.unpack('<I', g.memory(base + (capacity + 1)*4, 4))[0] == 0xDEADBEEF
        def key(k):
            tick = scalar('ticks')
            g.hmp('sendkey '+k+' 1')
            g.wait(lambda: scalar('ticks')-tick >= 3, 'key consumed')
        def result(test, level=0):
            addr, _ = g.symbol('pollikmark_results')
            return struct.unpack('<7IQIQ5I', g.memory(addr+(test*30+level)*68, 68))
        if full_run:
            g.wait(lambda: '[TEST] PHASE 2 PASS' in g.log.read_text() or
                   '[TEST] PHASE 2 FAIL' in g.log.read_text(),
                   'kernel startup PMM self-test completion', 90)
            assert '[TEST] PHASE 2 FAIL' not in g.log.read_text(), \
                   'kernel startup PMM self-test failed before PollikMark began'
            print('PASS startup PMM check completed before full PollikMark', flush=True)
            timer = g.stats()
            print(f"PollikMark timer: source={timer['clock_source']} "
                  f"resolution_us={timer['clock_resolution_us']} tsc_khz={timer['tsc_khz']}",
                  flush=True)
        key('f7')
        g.wait(lambda: scalar('g_focused_window') == 6 and g.presented(6), 'F7 painted')
        assert g.window(6)[4:6] == (680,410)
        if full_run:
            key('ret')
            g.wait(lambda: scalar('pollikmark_running') == 0 and
                   scalar('pollikmark_test') == 7 and scalar('pollikmark_level') == 29,
                   'all eight PollikMark workloads complete', 300)
            levels = (1,4,1,3,5,3,1,30)
            raw = [[result(t, level) for level in range(levels[t])] for t in range(8)]
            compositor_frames = list(g.words('pollikmark_compositor_frame_stats'))
            print(f"PollikMark compositor frame us: sampled={compositor_frames[0]}/"
                  f"{raw[6][0][1]} frames mean/p95/max={compositor_frames[1]}/"
                  f"{compositor_frames[2]}/{compositor_frames[3]}", flush=True)
            names = ('Fill Rate','2D Shapes','Triangle','Cube','Geometry','Texture','Compositor','Memory')
            units = ('px/s','shapes/s','tris/s','tris/s','tris/s','texpx/s','fps','B/s')
            rates = []
            for test in range(8):
                completed = [(level, row) for level, row in enumerate(raw[test]) if row[0] == 1]
                assert completed, (names[test], raw[test])
                selected = [row[7] for level, row in completed if not (test == 7 and level % 6 == 5)]
                assert selected, names[test]
                rate = sum(selected) // len(selected)
                states = [row[0] for row in raw[test]]
                rates.append(dict(name=names[test],rate=rate,unit=units[test],
                                  completed=len(completed),levels=levels[test],statuses=states))
                print(f"BASELINE {names[test]}: {rate} {units[test]} "
                      f"({len(completed)}/{levels[test]} levels; statuses={states})",flush=True)
            assert scalar('memory_bytes') == 0
            all_levels_completed = all(row[0] == 1 for test_rows in raw for row in test_rows)
            report = dict(status='PASS' if all_levels_completed else 'PARTIAL',
                          environment=g.environment,workloads=rates,raw_results=raw,
                          compositor_frame_us=compositor_frames,
                          timer=dict(clock_source=timer['clock_source'],
                                     clock_resolution_us=timer['clock_resolution_us'],
                                     tsc_khz=timer['tsc_khz']))
            safe = f"{g.accel}-{g.cpu}"
            (BUILD / f'pollikmark-full-{resolution}-{safe}.json').write_text(json.dumps(report,indent=2))
            state = 'PASS' if all_levels_completed else 'PARTIAL (not every level completed)'
            print(f'{resolution} full PollikMark {state}: accel={g.accel} cpu={g.cpu}',flush=True)
            return
        # Real registry icon uses opaque packed-nibble alpha; verify the generated
        # palette indexes and source alpha, plus actual dock hit/focus behavior.
        assert set(g.memory(*g.symbol('pollikmark_alpha'))) == {255}
        assert set(g.memory(*g.symbol('pollikmark_icon'))) == {0,1,2,3}
        dock_order = (1, 0, 2, 3, 4, 5, 6)  # Files is the permanent first item.
        for slot, app_id in enumerate(dock_order):
            x = g.width//2 - (g.apps-1)*34 + slot*68
            g.move(x,g.height-56)
            g.button(True);g.button(False)
            # Browser's bounded loader deliberately discards client launches;
            # retry PS/2 after it returns rather than interpreting this as a hit error.
            if app_id == 5:
                for _ in range(40):
                    if scalar('g_focused_window') == app_id:
                        break
                    key('f7')
                    g.button(True);g.button(False)
            try:
                g.wait(lambda: scalar('g_focused_window') == app_id,
                       f'dock slot {slot} -> app {app_id}')
            except AssertionError:
                print('dock probe state:', dict(slot=slot, expected_app=app_id,
                      pointer=g.pointer(), focused=scalar('g_focused_window'),
                      hover=g.words('shell')[-g.apps-1], window=g.window(app_id)), flush=True)
                raise
        guards()
        key('3')
        g.wait(lambda: result(2)[0] == 1, 'triangle finish', 20)
        tri = result(2)
        assert tri[0] == 1 and tri[1] >= 3 and tri[7] > 0, tri
        intervals = list(g.words('pollikmark_intervals')[:tri[1]])
        dist = distribution(intervals)
        assert tri[3] == dist['min_us'] and tri[4] == dist['max_us']
        assert abs(tri[5]-dist['low_1pct_fps']) < 1.1
        key('4')
        g.wait(lambda: scalar('pollikmark_running') == 1, 'cube start')
        key('esc');g.wait(lambda: scalar('pollikmark_running') == 0, 'cube cancellation')
        assert g.window(6)[7] == 1 and not g.window(6)[8]
        # Actual resize through maximize/restore contract.
        key('f11')
        g.wait(lambda: g.window(6)[4] != 680 and g.presented(6), 'maximize rendered')
        before_generation = scalar('pollikmark_generation')
        key('3')
        g.wait(lambda: scalar('pollikmark_generation') > before_generation, 'resized triangle started', 20)
        g.wait(lambda: scalar('pollikmark_running') == 0, 'resized triangle', 20)
        assert result(2)[0] == 1
        key('f11');g.wait(lambda: g.window(6)[4] == 680 and g.presented(6), 'restore rendered')
        key('8');g.wait(lambda: scalar('pollikmark_running') == 1, 'memory start')
        g.move(g.width//2,g.height//2)
        key('esc');g.wait(lambda: scalar('pollikmark_running') == 0, 'memory cancellation')
        assert scalar('memory_bytes') == 0
        key('7');g.wait(lambda: result(6)[0] == 1, 'compositor finish', 20)
        comp = result(6)
        assert comp[0] == 1 and comp[1] > 0 and comp[11] > 0 and comp[12] > 0, comp
        key('6');g.wait(lambda: scalar('pollikmark_running') == 0, 'unsupported texture')
        guards()
        # Save full screenshot; visual inspection catches layout/dock issues.
        image = BUILD / f'pollikmark-{resolution}.ppm'
        g.qmp('screendump', {'filename': str(image)})
        from PIL import Image
        Image.open(image).save(image.with_suffix('.png'))
        retained = result(6)
        key('alt-f4');g.wait(lambda: not g.window(6)[7], 'close')
        assert scalar('surface_bytes') == 0 and scalar('memory_bytes') == 0
        assert result(6) == retained
        key('f7');g.wait(lambda: g.presented(6), 'reopen')
        assert result(6) == retained
        guards()
        log = g.log.read_text()
        assert 'GUI MEMORY CORRUPTION' not in log and 'PANIC' not in log
        report = dict(status='PASS', environment=g.environment, triangle=tri,
                      triangle_intervals=intervals, distribution=dist, compositor=comp,
                      raw_results=list(g.words('pollikmark_results')))
        (BUILD / f'pollikmark-{resolution}.json').write_text(json.dumps(report, indent=2))
        print(resolution, 'PASS', 'triangle', tri, 'compositor', comp)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--resolution', choices=['1024x768','1920x1080'])
    p.add_argument('--full-run', action='store_true', help='run and report all eight PollikMark workloads')
    args = p.parse_args()
    for resolution in ([args.resolution] if args.resolution else ['1024x768','1920x1080']):
        run(resolution,args.full_run)
