"""train-motion-stats.py <frame log> [tick log] [--speed=N]: how smoothly the
watched train moves from paint to paint in a real-time run (TSRE_FRAME_LOG
with TSRE_WATCH_TRAIN; the capture harness's runFor with "runPaints", e.g.
tests/renderer/train-motion-katowice.json), the camera's steps when it
rides the train, and how the simulation's ticks went (TSRE_SIM_TICK_LOG).
--speed: the run's speed (runSpeed), for the expected steps.

Per paint (frames with a watched train moving): the paint interval, the
distance drawn since the paint before against speed x interval, repeats
(the same simulation frame drawn again), the frame's age at the paint."""
import csv
import statistics
import sys


def pct(values, p):
    v = sorted(values)
    return v[min(len(v) - 1, int(p / 100.0 * len(v)))] if v else float('nan')


def describe(name, values, unit=''):
    if not values:
        print('%-24s none' % name)
        return
    print('%-24s n %5d  median %7.2f  p10 %7.2f  p90 %7.2f  min %7.2f  max %7.2f %s' % (
        name, len(values), statistics.median(values), pct(values, 10), pct(values, 90), min(values), max(values), unit))


args = [a for a in sys.argv[1:] if not a.startswith('--')]
speedup = float(next((a.split('=')[1] for a in sys.argv[1:] if a.startswith('--speed=')), '1'))
sys.argv = [sys.argv[0]] + args
rows = list(csv.DictReader(open(sys.argv[1])))
paints = [r for r in rows if r.get('sim_tick') not in (None, '') and r.get('watch_train') not in ('', '-1')]
print('paints', len(rows), 'with a watched train', len(paints))
if paints:
    trains = sorted({r['watch_train'] for r in paints})
    print('watched trains', trains)
moving = []
prev = None
for r in paints:
    if prev is not None and r['watch_train'] == prev['watch_train']:
        dt = (float(r['t_ms']) - float(prev['t_ms'])) / 1000.0
        speed = 0.5 * (float(r['watch_mps']) + float(prev['watch_mps']))
        d = float(r['watch_m']) - float(prev['watch_m'])
        same = r['sim_tick'] == prev['sim_tick']
        cam = ((int(r['tile_x']) - int(prev['tile_x'])) * 2048.0 + float(r['x']) - float(prev['x']),
               (int(r['tile_z']) - int(prev['tile_z'])) * 2048.0 + float(r['z']) - float(prev['z']))
        if speed > 0.5 and dt > 0:
            expected = speed * dt * speedup
            moving.append({'dt': dt * 1000, 'd': d, 'expected': expected, 'ratio': d / expected, 'same': same,
                           'cam': (cam[0] ** 2 + cam[1] ** 2) ** 0.5,
                           'age': float(r['frame_age_ms']), 'ticks': int(r['sim_tick']) - int(prev['sim_tick']),
                           'speed': speed, 'view_ticks': int(r['view_tick']) - int(prev['view_tick'])})
    prev = r
print('paints while the watched train moves (> 0.5 m/s):', len(moving))
if moving:
    describe('paint interval', [m['dt'] for m in moving], 'ms')
    describe('speed', [m['speed'] for m in moving], 'm/s')
    describe('drawn / expected', [m['ratio'] for m in moving])
    describe('frame age at paint', [m['age'] for m in moving], 'ms')
    describe('sim ticks per paint', [m['ticks'] for m in moving])
    describe('view ticks per paint', [m['view_ticks'] for m in moving])
    same = sum(1 for m in moving if m['same'])
    print('paints drawing the same simulation frame again: %d of %d (%.1f %%)' % (same, len(moving), 100.0 * same / len(moving)))
    # Judder: how much the drawn step varies against the expected one.
    errors = [abs(m['d'] - m['expected']) * 100 for m in moving]
    describe('|drawn - expected|', errors, 'cm')
    describe('backward steps', [m['d'] * 100 for m in moving if m['d'] < 0], 'cm')
    if any(m['cam'] > 0.001 for m in moving):
        describe('camera step / expected', [m['cam'] / m['expected'] for m in moving])
    print('first 40 paints: interval ms / ticks / drawn cm / expected cm / age ms')
    for m in moving[:40]:
        print('  %6.1f  %d  %6.1f  %6.1f  %5.1f' % (m['dt'], m['ticks'], m['d'] * 100, m['expected'] * 100, m['age']))

if len(sys.argv) > 2:
    ticks = list(csv.DictReader(open(sys.argv[2])))
    running = [t for t in ticks if t['mode'] == '1']
    print()
    print('host ticks', len(ticks), 'running', len(running))
    describe('tick interval', [float(t['interval_ms']) for t in running], 'ms')
    describe('stepping', [float(t['step_ms']) for t in running], 'ms')
    describe('frame building', [float(t['publish_ms']) for t in running], 'ms')
    describe('steps per tick', [int(t['steps']) for t in running])
    describe('simulated per tick', [float(t['simulated_s']) * 1000 for t in running], 'ms')
    if running:
        wall = (float(running[-1]['t_ms']) - float(running[0]['t_ms'])) / 1000.0
        sim = sum(float(t['simulated_s']) for t in running)
        print('simulated %.2f s in %.2f s of wall time: %.3fx; trains %s' % (sim, wall, sim / wall if wall else 0,
                                                                        running[-1]['trains']))
