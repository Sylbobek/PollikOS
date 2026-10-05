"""Unit checks for guest timing ring extraction and percentile ranks."""
import time
from gui_metrics import COUNTERS, distribution, history_samples, stage_result

capacity = 128
before_index = 5
count = 140
ring = [None] * capacity
for sample in range(count):
    ring[(before_index + sample) % capacity] = sample
after_index = (before_index + count) % capacity
samples = history_samples(ring, after_index, count)
assert samples == list(range(12, 140)), (samples[:4], samples[-4:])
stats = distribution(list(range(1, 101)))
assert (stats['p50_us'], stats['p95_us'], stats['max_us']) == (50, 95, 100), stats
before = {name: 0 for name in COUNTERS}
after = dict(before, guest_ticks=1210, frame_write_index=0, interval_write_index=0,
             elapsed_us=0, duration_history=[0] * capacity, interval_history=[0] * capacity)
before.update(guest_ticks=10, frame_write_index=0, interval_write_index=0,
              elapsed_us=0, duration_history=[0] * capacity, interval_history=[0] * capacity)
idle = stage_result('idle_10s', before, after, time.monotonic() - 10, 0)
assert idle['duration_guest_s'] >= 9.9 and idle['interval_history']['count'] == 0, idle
after = dict(after, frame_count=1, elapsed_us=1000, total_time_us=1000,
             paint_time_us=300, compose_time_us=600, present_time_us=100,
             input_us=20, app_update_us=30, layout_us=40,
             paint_us=300, compose_us=600, present_us=100,
             composed_pixels_total=1920 * 1080,
             frame_write_index=1, interval_write_index=1,
             duration_history=[1000] + [0] * (capacity - 1),
             interval_history=[10_000_000] + [0] * (capacity - 1))
idle_wake = stage_result('idle_10s', before, after, time.monotonic() - 10, 1)
assert idle_wake['duration_guest_s'] >= 9.9 and idle_wake['interval_history']['p50_us'] == 10_000_000, idle_wake
print('PASS GUI timing metrics: wrapped history order; nearest-rank p50/p95/max; idle uses PIT elapsed and records wake-frame interval')
