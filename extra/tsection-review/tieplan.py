#!/usr/bin/env python3
"""2D prototype of the complex-track tie planner (tracks task 17, phase 1) on
real tsection shapes: groups of paths closer than a tie, a reference line,
stations along it, whole tracks clustered on each station's cut line into
shared bearers (up to MAX_BEARER), the hand-over to each path's own ties,
steep crossings keeping the first path's ties (lengthened under the other
track's rails). Writes one SVG plan per shape
(z up the page): blue bearers, brown own ties, red dots fasteners.

Usage: tieplan.py tsection.dat outdir name.s [name.s ...]
       a name prefixed "1a:" lays every tie square to the reference line."""
import sys, math, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tsection import parse

GAUGE = 1.435
TIE_LEN = 2.6
TIE_W = 0.26
SPACING = 0.6
PHASE = 0.3
OVERHANG = (TIE_LEN - GAUGE) / 2      # rail centre to tie end
MIN_GAP = 0.10                        # free space between two tie ends
MAX_BEARER = 4.8                      # longest shared tie
MAX_SHARED_ANGLE = 60.0               # steeper: the first track's ties run through
STEEP_LENGTHEN = True                 # steep: the first track's ties lengthened
                                      # under the other track's rails
BED_HALF = 2.3                        # ballast toe from the track centre
STEP = 0.1


def sample(p, secs):
    x, _, z = p['pos']; h = math.radians(p['rot']); s = 0.0
    pts = [(x, z, h, 0.0)]
    for k in p['sect']:
        d = secs[k]
        if 'radius' in d:
            a = math.radians(d['angle']); L = abs(a) * d['radius']
        else:
            a = 0.0; L = d['len']
        n = max(1, int(math.ceil(L / STEP)))
        for _ in range(n):
            seg = L / n; da = a / n; hm = h + da / 2
            x += seg * math.sin(hm); z += seg * math.cos(hm); h += da; s += seg
            pts.append((x, z, h, s))
    return pts


def offset(pts, d):
    # +d to the right of the heading
    return [(x + d * math.cos(h), z - d * math.sin(h), h, s) for x, z, h, s in pts]


def at(pts, s):
    if s <= 0: return pts[0]
    for i in range(1, len(pts)):
        if pts[i][3] >= s:
            a, b = pts[i - 1], pts[i]
            f = (s - a[3]) / max(1e-9, b[3] - a[3])
            return (a[0] + f * (b[0] - a[0]), a[1] + f * (b[1] - a[1]), a[2] + f * (b[2] - a[2]), s)
    return pts[-1]


def line_hits(o, n, poly):
    """Intersections of the line o + t*n with polyline poly: list of (t, heading)."""
    out = []
    for i in range(len(poly) - 1):
        ax, az = poly[i][0], poly[i][1]; bx, bz = poly[i + 1][0], poly[i + 1][1]
        rx, rz = bx - ax, bz - az
        den = n[0] * rz - n[1] * rx
        if abs(den) < 1e-12: continue
        qx, qz = ax - o[0], az - o[1]
        t = (qx * rz - qz * rx) / den
        u = (qx * n[1] - qz * n[0]) / den
        if 0 <= u < 1 and abs(t) <= 12.0:
            out.append((t, poly[i][2]))
    return out


def straightness(pts):
    return abs(pts[-1][2] - pts[0][2])


def rect(cx, cz, ux, uz, length, width):
    # tie footprint corners; u = direction along the tie
    vx, vz = -uz, ux
    hl, hw = length / 2, width / 2
    return [(cx + sx * hl * ux + sy * hw * vx, cz + sx * hl * uz + sy * hw * vz) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]


def overlap(r1, r2):
    # separating axis for two convex quads
    for r in (r1, r2):
        for i in range(4):
            ax, az = r[(i + 1) % 4][0] - r[i][0], r[(i + 1) % 4][1] - r[i][1]
            nx, nz = -az, ax
            p1 = [nx * x + nz * z for x, z in r1]; p2 = [nx * x + nz * z for x, z in r2]
            if max(p1) < min(p2) - 1e-6 or max(p2) < min(p1) - 1e-6:
                return False
    return True


def plan(shape, secs, own_ties=True):
    paths = [sample(p, secs) for p in shape['paths']]
    rails = [(offset(p, -GAUGE / 2), offset(p, GAUGE / 2)) for p in paths]
    n = len(paths)
    # groups: paths whose centrelines come closer than a tie plus a gap
    parent = list(range(n))
    def find(i):
        while parent[i] != i: i = parent[i]
        return i
    near = TIE_LEN + MIN_GAP
    for a in range(n):
        for b in range(a + 1, n):
            d = min(math.hypot(p[0] - q[0], p[1] - q[1]) for p in paths[a][::5] for q in paths[b][::5])
            if d < near:
                parent[find(a)] = find(b)
    groups = {}
    for i in range(n): groups.setdefault(find(i), []).append(i)
    ties, notes = [], []
    for members in groups.values():
        if len(members) == 1:
            i = members[0]; p = paths[i]
            s = PHASE
            while s <= p[-1][3] + 1e-6:
                x, z, h, _ = at(p, s)
                ties.append({'r': rect(x, z, math.cos(h), -math.sin(h), TIE_LEN, TIE_W), 'kind': 'own', 'path': i})
                s += SPACING
            continue
        # reference: a straight path on the shape's own axis; two straight paths that
        # cross: their bisector (steep: no shared ties); no straight path: the mean
        # of the two least curved paths from a shared start (wye, curved points)
        def on_axis(i):
            p = paths[i]
            return straightness(p) < 1e-4 and abs(p[0][0]) < 1e-3 and abs(p[0][1]) < 1e-3 and abs(p[0][2]) < 1e-4
        order = sorted(members, key=lambda i: (not on_axis(i), round(straightness(paths[i]), 4), round(abs(math.sin(paths[i][0][2])), 4), -paths[i][-1][3]))
        ref = order[0]
        heads = {i: paths[i][0][2] for i in members}
        straight = [i for i in members if straightness(paths[i]) < 1e-4]
        steep = False
        angle = 0.0
        crossing = None
        for ai in range(len(straight)):
            for bi in range(ai + 1, len(straight)):
                a, b = straight[ai], straight[bi]
                if abs(heads[a] - heads[b]) < 1e-4: continue
                hits = line_hits((paths[a][0][0], paths[a][0][1]), (math.cos(heads[a]), -math.sin(heads[a])), [])
                # do the centre lines cross inside both paths?
                pa, pb = paths[a], paths[b]
                for i in range(len(pa) - 1):
                    for j in range(0, len(pb) - 1):
                        ax, az, bx, bz = pa[i][0], pa[i][1], pa[i+1][0], pa[i+1][1]
                        cx, cz, dx, dz = pb[j][0], pb[j][1], pb[j+1][0], pb[j+1][1]
                        den = (bx-ax)*(dz-cz) - (bz-az)*(dx-cx)
                        if abs(den) < 1e-12: continue
                        t = ((cx-ax)*(dz-cz) - (cz-az)*(dx-cx)) / den
                        u = ((cx-ax)*(bz-az) - (cz-az)*(bx-ax)) / den
                        if 0 <= t < 1 and 0 <= u < 1 and crossing is None:
                            crossing = (a, b, ax + t*(bx-ax), az + t*(bz-az))
        if crossing is not None:
            a, b, cx, cz = crossing
            ha, hb_ = heads[a], heads[b]
            d = (hb_ - ha + math.pi / 2) % math.pi - math.pi / 2   # undirected difference
            hb_ = ha + d
            angle = abs(math.degrees(d))
            if angle > MAX_SHARED_ANGLE:
                steep = True
            else:
                hb = (ha + hb_) / 2
                # along the bisector: from the group's first point to its last
                proj = [(q[0] - cx) * math.sin(hb) + (q[1] - cz) * math.cos(hb)
                        for i in members for q in paths[i]]
                s0 = min(proj) - 1; s1 = max(proj) + 1
                k = int((s1 - s0) / STEP)
                refpts = [(cx + (s0 + j * STEP) * math.sin(hb), cz + (s0 + j * STEP) * math.cos(hb), hb, j * STEP) for j in range(k + 1)]
                notes.append(f'diamond {angle:.1f} deg: bisector of paths {a},{b}')
                ref = None
        elif not straight:
            # mean of the two least curved members from a shared start
            two = sorted(members, key=lambda i: straightness(paths[i]))[:2]
            pa, pb = paths[two[0]], paths[two[1]]
            L = min(pa[-1][3], pb[-1][3])
            refpts = []
            s = 0.0
            while s <= L + 1e-6:
                qa, qb = at(pa, s), at(pb, s)
                refpts.append(((qa[0]+qb[0])/2, (qa[1]+qb[1])/2, (qa[2]+qb[2])/2, s))
                s += STEP
            notes.append(f'mean of paths {two[0]},{two[1]}')
            ref = None
        if steep:
            notes.append(f'steep crossing {angle:.1f} deg: path {order[0]} ties through'
                         + (', lengthened' if STEEP_LENGTHEN else ''))
            prio = order
            for k, i in enumerate(prio):
                p = paths[i]; s = PHASE
                while s <= p[-1][3] + 1e-6:
                    x, z, h, _ = at(p, s)
                    nrm = (math.cos(h), -math.sin(h))
                    t0, t1 = -TIE_LEN / 2, TIE_LEN / 2
                    fast = [(x - GAUGE / 2 * nrm[0], z - GAUGE / 2 * nrm[1]), (x + GAUGE / 2 * nrm[0], z + GAUGE / 2 * nrm[1])]
                    kind = 'own'
                    if k == 0 and STEEP_LENGTHEN:
                        # the other tracks' rails crossing this tie's line, nearest first:
                        # carried while the tie stays within MAX_BEARER
                        others = sorted((abs(t), t) for j in members if j != i
                                        for side in (0, 1) for t, _ in line_hits((x, z), nrm, rails[j][side]))
                        for _, t in others:
                            a, b = min(t0, t - OVERHANG), max(t1, t + OVERHANG)
                            if b - a > MAX_BEARER:
                                break
                            if a < t0 or b > t1 or abs(t) <= TIE_LEN / 2:
                                t0, t1 = a, b
                                fast.append((x + t * nrm[0], z + t * nrm[1]))
                                kind = 'bearer'
                    tc = (t0 + t1) / 2
                    r = rect(x + tc * nrm[0], z + tc * nrm[1], nrm[0], nrm[1], t1 - t0, TIE_W)
                    if not any(overlap(r, t['r']) for t in ties):
                        ties.append({'r': r, 'kind': kind, 'path': i, 'fast': fast if kind == 'bearer' else []})
                    s += SPACING
            continue
        if ref is not None:
            refpts = paths[ref]
            notes.append(f'reference path {ref}')
        shared_s = {i: [] for i in members}   # path distances covered by stations
        covered = {i: -1.0 for i in members}  # path distance reached by the stations
        s = PHASE
        while s <= refpts[-1][3] + 1e-6:
            x, z, h, _ = at(refpts, s)
            nrm = (math.cos(h), -math.sin(h))
            hits = []
            for i in members:
                for side in (0, 1):
                    for t, hh in line_hits((x, z), nrm, rails[i][side]):
                        hits.append((t, i, side))
            hits.sort()
            if hits:
                # clusters of whole tracks (both rails of a path stay together):
                # must share where two tracks' tie ends would collide
                spans = {}
                for hp in hits:
                    spans.setdefault(hp[1], []).append(hp)
                units = sorted((sorted(v) for v in spans.values()), key=lambda u: u[0][0])
                clusters = [list(units[0])]
                for u in units[1:]:
                    if u[0][0] - max(h[0] for h in clusters[-1]) < 2 * OVERHANG + MIN_GAP:
                        clusters[-1] = sorted(clusters[-1] + u)
                    else:
                        clusters.append(list(u))
                # prefer shared: merge neighbours while the bearer stays short enough
                merged = [clusters[0]]
                for c in clusters[1:]:
                    span = c[-1][0] - merged[-1][0][0] + 2 * OVERHANG
                    if span <= MAX_BEARER:
                        merged[-1] = merged[-1] + c
                    else:
                        merged.append(c)
                for c in merged:
                    t0, t1 = c[0][0] - OVERHANG, c[-1][0] + OVERHANG
                    pathset = sorted({hp[1] for hp in c})
                    tc = (t0 + t1) / 2
                    # a cluster of one non-reference path (phase 1b): its own tie, square
                    # to itself, where that fits; else a tie square to the reference
                    if own_ties and len(pathset) == 1 and pathset[0] != ref:
                        i = pathset[0]
                        hh = line_hits((x, z), nrm, paths[i])
                        if hh:
                            px, pz = x + hh[0][0] * nrm[0], z + hh[0][0] * nrm[1]
                            # nearest sample of the path: its heading
                            q = min(paths[i], key=lambda q: (q[0] - px) ** 2 + (q[1] - pz) ** 2)
                            r = rect(px, pz, math.cos(q[2]), -math.sin(q[2]), TIE_LEN, TIE_W)
                            if not any(overlap(r, t['r']) for t in ties) and not any(
                                    overlap(r, rect(x + ((c2[0][0] + c2[-1][0]) / 2) * nrm[0], z + ((c2[0][0] + c2[-1][0]) / 2) * nrm[1],
                                                    nrm[0], nrm[1], c2[-1][0] - c2[0][0] + 2 * OVERHANG, TIE_W))
                                    for c2 in merged if c2 is not c):
                                ties.append({'r': r, 'kind': 'own', 'path': i})
                                covered[i] = max(covered[i], q[3])
                                continue
                        covered[i] = max(covered[i], q[3]) if hh else covered[i]
                    cx, cz = x + tc * nrm[0], z + tc * nrm[1]
                    kind = 'bearer' if len(pathset) > 1 else 'tie'
                    ties.append({'r': rect(cx, cz, nrm[0], nrm[1], t1 - t0, TIE_W), 'kind': kind, 'path': pathset,
                                 'fast': [(x + hp[0] * nrm[0], z + hp[0] * nrm[1]) for hp in c]})
                    for i in pathset:
                        hh = line_hits((x, z), nrm, paths[i])
                        if hh:
                            px, pz = x + hh[0][0] * nrm[0], z + hh[0][0] * nrm[1]
                            q = min(paths[i], key=lambda q: (q[0] - px) ** 2 + (q[1] - pz) ** 2)
                            covered[i] = max(covered[i], q[3])
            s += SPACING
        if own_ties:
            # phase 1b: each path's own ties where it is not on a shared bearer, if they fit
            for i in order:
                if i == ref: continue
                p = paths[i]; s = covered[i] + SPACING if covered[i] >= 0 else PHASE
                while s <= p[-1][3] + 1e-6:
                    x, z, h, _ = at(p, s)
                    r = rect(x, z, math.cos(h), -math.sin(h), TIE_LEN, TIE_W)
                    if not any(overlap(r, t['r']) for t in ties):
                        ties.append({'r': r, 'kind': 'own', 'path': i})
                    s += SPACING
    return paths, rails, ties, notes


def svg(name, shape, secs, out, own_ties=True):
    paths, rails, ties, notes = plan(shape, secs, own_ties)
    xs = [p[0] for path in paths for p in path]; zs = [p[1] for path in paths for p in path]
    pad = 3.0
    x0, x1, z0, z1 = min(xs) - pad, max(xs) + pad, min(zs) - pad, max(zs) + pad
    sc = 22.0
    W, H = (x1 - x0) * sc, (z1 - z0) * sc + 40
    def P(x, z): return f'{(x - x0) * sc:.1f},{(z1 - z) * sc + 30:.1f}'
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.0f}" height="{H:.0f}" viewBox="0 0 {W:.0f} {H:.0f}">',
         f'<rect width="100%" height="100%" fill="#f4f1ea"/>',
         f'<text x="6" y="18" font-family="sans-serif" font-size="14" fill="#222">{name}  ({"; ".join(notes)})</text>']
    # bed footprint per path (light)
    for path in paths:
        l = offset(path, -BED_HALF); r = offset(path, BED_HALF)
        pts = ' '.join(P(q[0], q[1]) for q in l + r[::-1])
        o.append(f'<polygon points="{pts}" fill="#c9c2b4" fill-opacity="0.45" stroke="none"/>')
    col = {'own': '#8a6a45', 'tie': '#6e5a44', 'bearer': '#3f6fa8'}
    for t in ties:
        pts = ' '.join(P(x, z) for x, z in t['r'])
        o.append(f'<polygon points="{pts}" fill="{col[t["kind"]]}" fill-opacity="0.85" stroke="#222" stroke-width="0.5"/>')
    for l, r in rails:
        for rl in (l, r):
            pts = ' '.join(P(q[0], q[1]) for q in rl[::2])
            o.append(f'<polyline points="{pts}" fill="none" stroke="#202020" stroke-width="1.6"/>')
    for t in ties:
        for x, z in t.get('fast', []):
            c = P(x, z).split(',')
            o.append(f'<circle cx="{c[0]}" cy="{c[1]}" r="1.6" fill="#d03030"/>')
    for i, path in enumerate(paths):
        pts = ' '.join(P(q[0], q[1]) for q in path[::4])
        o.append(f'<polyline points="{pts}" fill="none" stroke="#c03030" stroke-width="0.7" stroke-dasharray="4,3"/>')
        e = path[-1]; c = P(e[0], e[1]).split(',')
        o.append(f'<text x="{c[0]}" y="{float(c[1]) - 4}" font-family="sans-serif" font-size="11" fill="#c03030">{i}</text>')
    o.append('</svg>')
    path = os.path.join(out, name.replace('.s', '') + ('' if own_ties else '_allshared') + '.svg')
    open(path, 'w').write('\n'.join(o))
    kinds = {}
    for t in ties: kinds[t['kind']] = kinds.get(t['kind'], 0) + 1
    print(name, kinds, notes)
    return path


if __name__ == '__main__':
    secs, shapes = parse(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    by = {s.get('file', '').lower(): s for s in shapes.values()}
    for n in sys.argv[3:]:
        mode = True
        if n.startswith('1a:'):
            mode = False; n = n[3:]
        s = by.get(n.lower())
        if s is None:
            print('missing', n); continue
        svg(s['file'], s, secs, out, mode)
