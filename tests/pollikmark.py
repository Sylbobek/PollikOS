"""Real QEMU, verified ELF image, PS/2-only actions and read-only probes.
No synthetic guest calls/memory writes; retain screenshot and raw results.
"""
import argparse
import json
import struct
from gui_metrics import Guest, BUILD, distribution


def run(resolution):
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
            return struct.unpack('<15I', g.memory(addr+(test*30+level)*60, 60))
        key('f7')
        g.wait(lambda: scalar('g_focused_window') == 6 and g.presented(6), 'F7 painted')
        assert g.window(6)[4:6] == (680,410)
        # Real registry icon uses opaque packed-nibble alpha; verify the generated
        # palette indexes and source alpha, plus actual dock hit/focus behavior.
        assert set(g.memory(*g.symbol('pollikmark_alpha'))) == {255}
        assert set(g.memory(*g.symbol('pollikmark_icon'))) == {0,1,2,3}
        for i in range(7):
            x = g.width//2 - (g.apps-1)*34 + i*68
            g.move(x,g.height-56)
            g.button(True);g.button(False)
            # Browser's bounded loader deliberately discards client launches;
            # retry PS/2 after it returns rather than interpreting this as a hit error.
            if i == 6:
                for _ in range(40):
                    if scalar('g_focused_window') == i:
                        break
                    key('f7')
                    g.button(True);g.button(False)
            g.wait(lambda: scalar('g_focused_window') == i, f'dock slot {i}')
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
    args = p.parse_args()
    for resolution in ([args.resolution] if args.resolution else ['1024x768','1920x1080']):
        run(resolution)
