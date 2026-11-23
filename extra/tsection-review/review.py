#!/usr/bin/env python3
"""Group a tsection.dat's track shapes by geometry for procedural track work
(tracks task 17): single paths, separate parallel paths, junctions, diamond
crossings, overlapping pieces (frog pieces), plus name groups (tunnels,
bridges, buffers, turntables, crossings) that need more than a track profile.

Usage:
  review.py GLOBAL/tsection.dat [--prefix 'A\\d+t'] [--csv out.csv]
            [--world ROUTES/<route>/WORLD ...] [--shapes GLOBAL/SHAPES ...]
            [--jobs 8]

--world counts the route's TrackObjs per TrackShape by their SectionIdx, so
reskinned shapes (DBTracks' DB/DB30_A1tPnt7_5dLft.s) count with the shape
whose geometry they use. Only text (UTF-16 or ASCII) W files are read;
compressed ones are skipped and counted.

Needs numpy."""
import argparse
import collections
import csv
import math
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tsection import parse, sample  # noqa: E402

GAUGE = 1.435
TIE = 2.6          # a plain tie: paths closer than this share ties somewhere
STEP = 0.25

NAME_GROUPS = [
    ('turntable/transfer/roundhouse', r'Turntable|Roundh|Slidingtable|trnsfer|Carfloat|Fordito'),
    ('buffer', r'Buffer'),
    ('level/pedestrian crossing', r'LvlCrs|PedesCrs|Gate'),
    ('scale/water/fuel/derail/dummy', r'Scale|Wtr|Fuel|Derail|Dmy'),
    ('tunnel', r'Tun'),
    ('bridge', r'Brdg|Bridge'),
]


def rails(p):
    a = np.array(p)
    nx, nz = np.cos(a[:, 2]), -np.sin(a[:, 2])
    g = GAUGE / 2
    return (np.stack([a[:, 0] - g * nx, a[:, 1] - g * nz], 1),
            np.stack([a[:, 0] + g * nx, a[:, 1] + g * nz], 1))


def crossings(A, B):
    """Intersections of polylines A and B: [(i, j, point, angle_deg)]."""
    a0, a1, b0, b1 = A[:-1], A[1:], B[:-1], B[1:]
    amin, amax = np.minimum(a0, a1), np.maximum(a0, a1)
    bmin, bmax = np.minimum(b0, b1), np.maximum(b0, b1)
    out = []
    for i in range(len(a0)):
        m = ((bmax[:, 0] >= amin[i, 0] - 1e-6) & (bmin[:, 0] <= amax[i, 0] + 1e-6)
             & (bmax[:, 1] >= amin[i, 1] - 1e-6) & (bmin[:, 1] <= amax[i, 1] + 1e-6))
        for j in np.nonzero(m)[0]:
            p, r = a0[i], a1[i] - a0[i]
            q, s = b0[j], b1[j] - b0[j]
            den = r[0] * s[1] - r[1] * s[0]
            if abs(den) < 1e-12:
                continue
            t = ((q - p)[0] * s[1] - (q - p)[1] * s[0]) / den
            u = ((q - p)[0] * r[1] - (q - p)[1] * r[0]) / den
            if 0 <= t < 1 and 0 <= u < 1:
                c = abs(np.dot(r, s)) / (np.linalg.norm(r) * np.linalg.norm(s))
                out.append((i, j, p + t * r, math.degrees(math.acos(min(1.0, c)))))
    return out


def analyse(shape, sections):
    P = [sample(p, sections, STEP) for p in shape['paths']]
    if any(p is None for p in P):
        return None
    n = len(P)
    res = {'paths': n, 'lengths': [round(p[-1][3], 2) for p in P],
           'switches': 0, 'rail_crossings': 0, 'crossing_angles': [], 'tie_overlap': 0.0}
    if n < 2:
        return res
    R = [rails(p) for p in P]
    C = [np.array(p) for p in P]
    found = []
    for a in range(n):
        for b in range(a + 1, n):
            same = (math.hypot(P[a][0][0] - P[b][0][0], P[a][0][1] - P[b][0][1]) < 0.05
                    and abs(P[a][0][2] - P[b][0][2]) < 1e-3)
            res['switches'] += int(same)
            for ra in range(2):
                for rb in range(2):
                    for i, j, pt, ang in crossings(R[a][ra], R[b][rb]):
                        if (same and i < 4 and j < 4) or ang < 0.05:
                            continue
                        if not any(np.hypot(*(pt - q)) < 0.3 for q, _ in found):
                            found.append((pt, ang))
    res['rail_crossings'] = len(found)
    res['crossing_angles'] = sorted({round(min(a, 180 - a), 1) for _, a in found})
    # length of path closer than a tie to another path's centre line
    for a in range(n):
        seg = np.diff(C[a][:, 3])
        best = np.full(len(C[a]) - 1, 1e9)
        for b in range(n):
            if b != a:
                d = np.hypot(C[a][:-1, None, 0] - C[b][None, :, 0], C[a][:-1, None, 1] - C[b][None, :, 1]).min(1)
                best = np.minimum(best, d)
        res['tie_overlap'] += float(seg[best < TIE].sum())
    res['tie_overlap'] = round(res['tie_overlap'], 1)
    return res


def group(shape, a):
    name = re.sub(r'\.s$', '', shape.get('file', ''), flags=re.I)
    if 'roadshape' in shape['flags']:
        return '9 road'
    for label, pat in NAME_GROUPS:
        if re.search(pat, name, re.I):
            if label == 'tunnel' or 'tunnelshape' in shape['flags']:
                return '6 tunnel'
            if label == 'bridge':
                return '7 bridge'
            return '8 ' + label
    if 'tunnelshape' in shape['flags']:
        return '6 tunnel'
    if a['paths'] == 1:
        return '1 single path'
    if not (a['tie_overlap'] > 0 or a['rail_crossings'] or a['switches']):
        return '2 parallel paths, separate'
    if a['switches']:
        return '3 junction'
    if a['rail_crossings']:
        return '4 diamond crossing'
    return '5 overlapping paths (frog pieces)'


def world_uses(dirs):
    uses = collections.Counter()
    skipped = 0
    rx = re.compile(r'TrackObj\s*\((.*?)\n\s*\)', re.S)
    for d in dirs:
        for f in os.listdir(d):
            if not f.lower().endswith('.w'):
                continue
            b = open(os.path.join(d, f), 'rb').read()
            if b[:2] == b'\xff\xfe':
                t = b.decode('utf-16-le', 'replace')
            elif b[:8] == b'SIMISA@@' and b'JINX0w0t' in b[:40]:
                t = b.decode('latin-1')
            else:
                skipped += 1
                continue
            for m in rx.finditer(t):
                s = re.search(r'SectionIdx\s*\(\s*(\d+)', m.group(1))
                if s:
                    uses[int(s.group(1))] += 1
    return uses, skipped


def shape_index(dirs):
    """Lower-case shape file names found under the given SHAPES folders
    (any depth) -> the folders' labels."""
    found = collections.defaultdict(set)
    for d in dirs:
        label = d.rstrip('/').split('/')[-3] if d.rstrip('/').lower().endswith('global/shapes') else d
        for root, _, files in os.walk(d):
            for f in files:
                if f.lower().endswith('.s'):
                    found[f.lower()].add(label)
    return found


_SECTIONS = None


def _work(item):
    idx, s = item
    return idx, analyse(s, _SECTIONS)


def _init(sections):
    global _SECTIONS
    _SECTIONS = sections


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tsection')
    ap.add_argument('--prefix', default='')
    ap.add_argument('--csv')
    ap.add_argument('--world', nargs='*', default=[])
    ap.add_argument('--shapes', nargs='*', default=[],
                    help='GLOBAL/SHAPES folders: which shape files exist locally')
    ap.add_argument('--jobs', type=int, default=1)
    args = ap.parse_args()
    sections, shapes = parse(args.tsection)
    uses, skipped = world_uses(args.world) if args.world else (collections.Counter(), 0)
    local = shape_index(args.shapes) if args.shapes else {}
    todo = [(idx, s) for idx, s in sorted(shapes.items())
            if not args.prefix or re.match(args.prefix, s.get('file', ''), re.I)]
    if args.jobs > 1:
        import multiprocessing
        with multiprocessing.Pool(args.jobs, _init, (sections,)) as pool:
            done = dict(pool.map(_work, todo, chunksize=8))
    else:
        _init(sections)
        done = dict(map(_work, todo))
    rows = []
    for idx, s in todo:
        f = s.get('file', '')
        a = done[idx]
        base = {'idx': idx, 'file': f, 'info': ' | '.join(s.get('info', [])),
                'flags': ' '.join(x for x in s['flags'] if x not in ('crossovershape',)),
                'local': ' '.join(sorted(local.get(f.lower(), ()))), 'uses': uses.get(idx, 0)}
        gauges = {round(sections[k]['gauge'], 3) for p in s['paths'] for k in p['sect']
                  if k in sections and 'gauge' in sections[k]}
        base['gauge'] = ' '.join(str(g) for g in sorted(gauges))
        if a is None:
            rows.append({**base, 'group': '0 missing section'})
            continue
        rows.append({**base, 'group': group(s, a), 'mainroute': s.get('mainroute', ''),
                     'xover': 'xover' in s, **a})
    if args.csv:
        keys = ['idx', 'file', 'group', 'paths', 'lengths', 'gauge', 'mainroute', 'xover', 'switches',
                'rail_crossings', 'crossing_angles', 'tie_overlap', 'flags', 'uses', 'local', 'info']
        with open(args.csv, 'w', newline='') as fh:
            w = csv.DictWriter(fh, keys, extrasaction='ignore')
            w.writeheader()
            w.writerows(rows)
    by = collections.defaultdict(list)
    for r in rows:
        by[r['group']].append(r)
    print(f'{len(rows)} shapes' + (f'; {sum(uses.values())} TrackObjs counted, {skipped} W files skipped'
                                   if args.world else ''))
    for g in sorted(by):
        rs = by[g]
        line = f'{g:34} {len(rs):5} shapes'
        if args.world:
            line += f'  {sum(r.get("uses", 0) for r in rs):6} objects in {sum(1 for r in rs if r.get("uses"))} shapes'
        if args.shapes:
            line += f'  {sum(1 for r in rs if r.get("local"))} found locally'
        print(line)
        ex = sorted(rs, key=lambda r: (-r.get('uses', 0), r['idx']))[:5]
        print('      e.g. ' + ', '.join(r['file'] for r in ex))


if __name__ == '__main__':
    main()
