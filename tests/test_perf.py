"""Assert live GUI timing, counters, percentiles and actual presentation at two sizes."""
import argparse
import json
import os
import pathlib
import subprocess
import tempfile
import time

from gui_metrics import BUILD, ROOT, Guest, distribution


def native_stats():
    # Compile the actual production accounting/snapshot/summary functions with
    # a deterministic clock; hardware clock itself is checked in QEMU below.
    source = (ROOT / 'kernel/wm.c').read_text()
    source = source[source.index('void wm_perf_frame_begin(void)'):]
    prelude = r'''
#include "wm.h"
extern int printf(const char *, ...);
#define PERF_HISTORY_SIZE 128
GuiPerfStats g_perf_stats;
static int g_perf_overlay_enabled;
static int g_perf_pending_frame_kind;
static u64 now, g_frame_start, g_previous_present, g_rate_start;
static u32 g_sec_input_events, g_sec_coalesced_mouse, g_sec_client_paints;
static u32 g_sec_compositor_frames, g_sec_presents;
static u64 g_sec_pixels_composed, g_sec_pixels_presented;
static u32 g_frame_time_history[128], g_frame_interval_history[128];
static u32 g_frame_time_idx, g_interval_idx;
static u64 perf_div(u64 n, u32 d) { return n / d; }
u64 wm_time_us(void) { return now; }
static int wm_is_visible(int i) { return i < 5; }
#define CHECK(x) do { if (!(x)) { printf("FAIL native telemetry line %d\n", __LINE__); return 1; } } while (0)
'''
    oracle = r'''
int main(void) {
    GuiPerfStats s;
    char out[300];
    for (int cap = 0; cap < 250; cap++) {
        for (int j = 0; j < 300; j++) out[j] = '#';
        wm_perf_summary(out + 1, cap);
        CHECK(out[0] == '#' && out[cap + 1] == '#');
        if (cap) { int j = 1; while (j <= cap && out[j]) j++; CHECK(j <= cap); }
    }
    for (u32 i = 1; i <= 128; i++) {
        now = (u64)i * i * 10000;
        wm_perf_frame_begin();
        now += i;
        g_perf_stats.composed_pixels = 1292;
        g_perf_stats.effective_rects = 1;
        wm_perf_frame_end(0, 0, 0, 0, 2000);
    }
    wm_perf_snapshot(&s);
    CHECK(s.history_count == 128 && s.interval_count == 127);
    CHECK(s.partial_frames == 128 && s.full_redraw_count == 0);
    CHECK(s.avg_frame_us == 64 && s.min_frame_us == 1 && s.max_frame_us == 128);
    CHECK(s.p95_frame_us == 122 && s.p99_frame_us == 127);
    CHECK(s.slow_1pct_interval_us == 2540001 && s.low_1pct_fps == 0);
    CHECK(s.p95_interval_us == 2430001 && s.p99_interval_us == 2530001);
    CHECK(s.render_fps == 15625 && s.frame_count == 128);
    CHECK(s.total_time_us == 8256 && s.compose_time_us == 8256);
    CHECK(s.composed_pixels_total == 128 * 1292 && s.presented_pixels_total == 128 * 2000);
    now += 2000000; wm_perf_snapshot(&s);
    now += 2000000; wm_perf_snapshot(&s);
    CHECK(s.fps == 0 && s.presents_sec == 0);
    now += 1; wm_perf_frame_begin(); now += 1000;
    wm_perf_frame_end(0, 300, 200, 1, 3000);
    wm_perf_snapshot(&s);
    CHECK(s.partial_frames == 128 && s.full_redraw_count == 1);
    CHECK(s.total_us == 1000 && s.paint_us == 300 && s.present_us == 200 && s.compose_us == 500);
    CHECK(s.history_count == 128 && s.p99_frame_us == 128 && s.max_frame_us == 1000);
    printf("PASS native: exact percentiles/interval-low/history wrap/idle FPS/bounded summary\n");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='pollikos-perf-native-') as folder:
        file = pathlib.Path(folder) / 'stats.c'
        exe = pathlib.Path(folder) / ('stats.exe' if os.name == 'nt' else 'stats')
        file.write_text(prelude + source + oracle)
        cmd = ['clang', '-O2', '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'kernel'), str(file), '-o', str(exe)]
        if os.name == 'nt':
            cmd += ['-fuse-ld=lld']
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe)], check=True)


def run(resolution):
    with Guest(resolution, 'perf') as g:
        start = time.monotonic()
        s = g.capture()
        assert s['clock_source'] == 1 and s['clock_resolution_us'] == 1, s
        assert 1000 <= s['tsc_khz'] <= 10000000, s
        g.key('f3', lambda: g.window()[7] == 1, 'terminal failed to open')
        g.wait(lambda: g.presented(), 'terminal not presented')
        g.move(g.width - 55, 65)  # Away from clients and dock, cursor-only damage.
        g.wait(lambda: g.words('shell')[2:4] == (0, 0), 'scene not clean')
        before = g.capture()
        for i in range(12):
            count = g.stats()['cursor_frames']
            g.move(g.width - 55 + (i % 2) * 12, 65 + (i % 2) * 5)
            if i:
                g.wait(lambda: g.stats()['cursor_frames'] > count, 'cursor path not counted')
        after = g.capture()
        assert after['cursor_frames'] > before['cursor_frames']
        assert after['client_paint_count'] == before['client_paint_count'], 'cursor repainted client'
        assert after['paint_time_us'] == before['paint_time_us'], 'cursor charged as paint'
        assert after['present_time_us'] > before['present_time_us'], 'no timed framebuffer copy'
        assert after['composed_pixels_total'] - before['composed_pixels_total'] < after['presented_pixels_total'] - before['presented_pixels_total']
        # Real maximize and restore must render fresh geometry, not cached metadata.
        for state in (2, 0):
            g.key('f11', lambda: g.window()[6] == state, 'maximize/restore transition')
            g.wait(lambda: g.presented(), 'resized window not presented')
        g.perf_command()  # Refresh the read-only history snapshot through existing host.
        s = g.capture()
        assert s['history_count'] > 0 and s['interval_count'] > 0
        assert s['frame_count'] == (s['full_redraw_count'] + s['cursor_frames'] +
                                    s['dock_frames'] + s['partial_frames']), s
        assert s['total_time_us'] == s['paint_time_us'] + s['compose_time_us'] + s['present_time_us'], s
        assert s['paint_time_us'] > 0 and s['compose_time_us'] > 0 and s['present_time_us'] > 0
        assert s['total_us'] == s['paint_us'] + s['compose_us'] + s['present_us']
        # Snapshot is evaluated before the perf command paints its own output;
        # validate its exact ring at that moment using a stable idle summary
        # from the kernel rather than treating max and second-max as percentiles.
        assert 0 < s['min_frame_us'] <= s['avg_frame_us'] <= s['max_frame_us'] <= s['worst_frame_us']
        assert s['p95_frame_us'] <= s['p99_frame_us'] <= s['max_frame_us']
        assert s['p95_interval_us'] <= s['p99_interval_us']
        assert s['low_1pct_fps'] == 1000000 // s['slow_1pct_interval_us']
        assert s['render_fps'] == 1000000 // s['avg_frame_us']
        assert s['damage_rects_count'] >= s['frame_count']
        assert any(v % 1000 for v in s['duration_history']), 'fake millisecond precision'
        assert not any(x in g.log.read_text() for x in ('GUI MEMORY CORRUPTION', 'KERNEL PANIC'))
        result = dict(environment=g.environment, duration_s=time.monotonic() - start,
                      stats=s, history=distribution(s['duration_history']),
                      assertions='clock/cursor cache/copy/surface+scene+LFB/accounting/percentile ordering')
        path = BUILD / f'perf-{resolution}.json'
        path.write_text(json.dumps(result, indent=2))
        print(f'PASS {resolution}: {s["frame_count"]} frames, clock={s["tsc_khz"]} kHz; {path}', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--resolution', action='append')
    args = parser.parse_args()
    native_stats()
    for resolution in args.resolution or ['1024x768', '1920x1080']:
        run(resolution)


if __name__ == '__main__':
    main()
