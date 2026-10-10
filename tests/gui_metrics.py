"""Portable timing distribution and history helpers.

The retired legacy guest driver is intentionally absent. Callers supply
counters and history samples; these helpers do not launch a VM or access disks.
"""
import time

PREFIX = ('frame_count fps avg_frame_us p95_frame_us p99_frame_us worst_frame_us '
          'input_events_sec coalesced_mouse_sec client_paints_sec compositor_frames_sec '
          'presents_sec pixels_composed_sec pixels_presented_sec full_redraw_count '
          'damage_rects_count layout_us paint_us present_us input_us compose_us window_count').split()
EXTRA = ('total_us min_frame_us max_frame_us history_count interval_count avg_interval_us '
         'p95_interval_us p99_interval_us low_1pct_fps slow_1pct_interval_us render_fps '
         'clock_source clock_resolution_us tsc_khz effective_rects composed_pixels '
         'presented_pixels cursor_frames dock_frames client_paint_count app_update_us').split()
TAIL = ('elapsed_us total_time_us paint_time_us compose_time_us present_time_us '
        'composed_pixels_total presented_pixels_total').split()
APPEND = ('partial_frames').split()
COUNTERS = ('frame_count full_redraw_count damage_rects_count cursor_frames dock_frames partial_frames '
            'client_paint_count').split() + TAIL[1:]


def distribution(values):
    if not values:
        return dict(count=0, mean_us=0, min_us=0, max_us=0, p50_us=0, p95_us=0, p99_us=0, low_1pct_fps=0)
    v = sorted(values)
    n = len(v)
    slow = v[-((n + 99) // 100):]
    return dict(count=n, mean_us=sum(v) / n, min_us=v[0], max_us=v[-1],
                p50_us=v[(n * 50 + 99) // 100 - 1],
                p95_us=v[(n * 95 + 99) // 100 - 1], p99_us=v[(n * 99 + 99) // 100 - 1],
                low_1pct_fps=1e6 * len(slow) / sum(slow) if sum(slow) else 0)


def history_samples(history, write_index, count):
    """Return the newest count samples in chronological order from a ring."""
    capacity = len(history)
    n = min(max(0, count), capacity)
    if not n:
        return []
    start = (write_index - n) % capacity
    return [history[(start + i) % capacity] for i in range(n)]


def stage_result(name, before, after, started, operations):
    elapsed = after['elapsed_us'] - before['elapsed_us']
    counts = {k: after[k] - before[k] for k in COUNTERS}
    if name.startswith('idle'):
        guest_ticks = (after['guest_ticks'] - before['guest_ticks']) & 0xffffffff
        elapsed = guest_ticks * 9943000000 // 1193180
        assert elapsed > 0, f'{name}: guest PIT did not advance'
    if counts['frame_count'] == 0:
        assert name.startswith('idle'), f'{name}: no completed presents'
        return dict(name=name, duration_host_s=time.monotonic() - started,
                    duration_guest_s=elapsed / 1e6, operations=operations, counts=counts,
                    actual_fps=0, render_throughput_fps=0, mean_paint_us=0,
                    mean_compose_us=0, mean_present_us=0,
                    mean_composed_pixels_per_frame=0,
                    phase_us=dict(input=0, app_update=0, layout=0, draw=0, composition=0, present=0),
                    frame_history=distribution([]), interval_history=distribution([]),
                    history_scope='no presents occurred during idle; no frame intervals exist', stats=after)
    assert elapsed > 0 and all(v >= 0 for v in counts.values()), (name, counts)
    assert counts['total_time_us'] >= counts['paint_time_us'] + counts['present_time_us']
    assert counts['total_time_us'] == counts['paint_time_us'] + counts['compose_time_us'] + counts['present_time_us']
    frame_samples = history_samples(after['duration_history'], after['frame_write_index'], counts['frame_count'])
    # interval_count is a capped occupancy value, not a cumulative counter.
    # Each completed frame after boot also records one presentation interval.
    sample_count = min(counts['frame_count'], len(after['interval_history']))
    interval_samples = history_samples(after['interval_history'], after['interval_write_index'], sample_count)
    return dict(name=name, duration_host_s=time.monotonic() - started,
                duration_guest_s=elapsed / 1e6, operations=operations, counts=counts,
                actual_fps=counts['frame_count'] * 1e6 / elapsed,
                render_throughput_fps=counts['frame_count'] * 1e6 / counts['total_time_us'],
                mean_paint_us=counts['paint_time_us'] / counts['frame_count'],
                mean_compose_us=counts['compose_time_us'] / counts['frame_count'],
                mean_present_us=counts['present_time_us'] / counts['frame_count'],
                mean_composed_pixels_per_frame=counts['composed_pixels_total'] / counts['frame_count'],
                phase_us=dict(input=after['input_us'], app_update=after['app_update_us'],
                              layout=after['layout_us'], draw=after['paint_us'],
                              composition=after['compose_us'], present=after['present_us']),
                frame_history=distribution(frame_samples),
                interval_history=distribution(interval_samples),
                history_scope='latest guest samples in stage; capped at 128; ordered by ring write index',
                stats=after)
