#!/usr/bin/env python3
"""Preview of a track assembly made of several W-file objects (tracks task 17):
a centre TrackObj and its neighbours within a radius, drawn in the centre
object's frame (z up the page), three panels:

  static    the objects' static shapes from above (distance level 0
            triangles; ballast light, sleepers brown, rails black, by height)
  filters   tie plans with the filter values: --excluded-parts leaves
            parts out of an object (Tie, Fastener, Rail); an object with
            --included-track-shape plans its ties and fastenings over those
            shapes' paths too (their rails only with --included-rails)
  plain     every object lays its own ties (no filter values)

Usage:
  assembly.py tsection.dat WORLD/w-file.w UID out.svg
              [--radius 20] [--shapes GLOBAL/SHAPES]
              [--excluded-parts PATTERN PART[,PART...]]
              [--included-track-shape PATTERN SHAPE X Y Z ROTY]
              [--included-rails]

Patterns match the W FileName (any case, * and ?)."""
import argparse
import fnmatch
import math
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tieplan as T  # noqa: E402
from tsection import parse  # noqa: E402


# --- W file -----------------------------------------------------------------

def track_objects(wfile):
    b = open(wfile, 'rb').read()
    t = b.decode('utf-16-le', 'replace') if b[:2] == b'\xff\xfe' else b.decode('latin-1')
    out = []
    for m in re.finditer(r'TrackObj\s*\((.*?)\n\s*\)', t, re.S):
        blk = m.group(1)
        num = r'([-\d.eE+]+)'
        out.append({
            'uid': int(re.search(r'UiD\s*\(\s*(\d+)', blk).group(1)),
            'sect': int(re.search(r'SectionIdx\s*\(\s*(\d+)', blk).group(1)),
            'file': re.search(r'FileName\s*\(\s*"?([^\s")]+)', blk).group(1),
            'pos': tuple(map(float, re.search(r'Position\s*\(\s*' + r'\s+'.join([num] * 3), blk).groups())),
            'q': tuple(map(float, re.search(r'QDirection\s*\(\s*' + r'\s+'.join([num] * 4), blk).groups())),
        })
    return out


def yaw(obj):
    # W QDirection (0 qy 0 qw): the heading in shape space's sense (positive
    # right) is -2 atan2(qy, qw); checked by the joints of PRUDNIK's double
    # slips meeting to the centimetre.
    return -2 * math.atan2(obj['q'][1], obj['q'][3])


def to_world(obj, x, z):
    th = yaw(obj)
    return (obj['pos'][0] + x * math.cos(th) + z * math.sin(th),
            obj['pos'][2] - x * math.sin(th) + z * math.cos(th))


def to_local(centre, wx, wz):
    th = yaw(centre)
    dx, dz = wx - centre['pos'][0], wz - centre['pos'][2]
    return (dx * math.cos(th) - dz * math.sin(th), dx * math.sin(th) + dz * math.cos(th))


# --- static shapes (binary, compressed or not) ------------------------------

def _blocks(d, i, end):
    while i + 8 <= end:
        tid, _, ln = struct.unpack_from('<HHI', d, i)
        yield tid, i + 8, i + 8 + ln
        i += 8 + ln


def _body(d, s):
    return s + 1 + 2 * d[s]


def _child(d, s, e, tid, counted=False, skip=0):
    # counted: the parent is a list, its body starts with the item count;
    # skip: bytes of plain values before the parent's child blocks
    for t, bs, be in _blocks(d, _body(d, s) + (4 if counted else 0) + skip, e):
        if t == tid:
            return bs, be
    return None


def _list(d, s, e):
    """A list block's items: [(tid, start, end)] after its count."""
    return list(_blocks(d, _body(d, s) + 4, e))


def _string(d, s):
    k = _body(d, s)
    n, = struct.unpack_from('<H', d, k)
    return d[k + 2:k + 2 + 2 * n].decode('utf-16-le')


def shape_triangles(path):
    """Distance level 0 triangles [((p0, p1, p2), (uv0, uv1, uv2), image)],
    points (x, y, z); None when the file is not a binary shape."""
    b = open(path, 'rb').read()
    if b[:8] == b'SIMISA@F':
        d = zlib.decompress(b[16:])
    elif b[:8] == b'SIMISA@@':
        d = b[16:]
    else:
        return None
    if not d.startswith(b'JINX0s1b'):
        return None
    top = next(_blocks(d, 16, len(d)))
    s, e = top[1], top[2]
    P = [struct.unpack_from('<fff', d, _body(d, bs)) for _, bs, _ in _list(d, *_child(d, s, e, 7))]
    UV = [struct.unpack_from('<ff', d, _body(d, bs)) for _, bs, _ in _list(d, *_child(d, s, e, 9))]
    images = [_string(d, bs) for _, bs, _ in _list(d, *_child(d, s, e, 14))]
    textures = [struct.unpack_from('<I', d, _body(d, bs))[0] for _, bs, _ in _list(d, *_child(d, s, e, 16))]
    states = []
    for _, bs, be in _list(d, *_child(d, s, e, 55)):           # prim_states
        ti = _child(d, bs, be, 61, skip=8)                    # tex_idxs after flags, shader
        k = _body(d, ti[0])
        n, = struct.unpack_from('<I', d, k)
        tex = struct.unpack_from('<I', d, k + 4)[0] if n else None
        states.append(images[textures[tex]] if tex is not None and tex < len(textures) else '')
    lc = _child(d, s, e, 31)                          # lod_controls
    lc = _child(d, *lc, 32, True)                     # lod_control
    dls = _child(d, *lc, 36)                          # distance_levels
    dl = _child(d, *dls, 37, True)                    # first distance_level
    subs = _child(d, *dl, 38)                         # sub_objects
    tris = []
    for t, ss, se in _list(d, *subs):
        if t != 39:
            continue
        V = []
        for _, vs, ve in _list(d, *_child(d, ss, se, 50)):    # vertices
            k = _body(d, vs)
            point, = struct.unpack_from('<I', d, k + 4)
            uvs = _child(d, vs, ve, 49, skip=20)              # vertex_uvs after 5 values
            uv = 0
            if uvs:
                kk = _body(d, uvs[0])
                n, = struct.unpack_from('<I', d, kk)
                uv = struct.unpack_from('<I', d, kk + 4)[0] if n else 0
            V.append((point, uv))
        state = ''
        for t2, ps, pe in _list(d, *_child(d, ss, se, 53)):   # primitives
            if t2 == 56:                                      # prim_state_idx
                si, = struct.unpack_from('<I', d, _body(d, ps))
                state = states[si] if si < len(states) else ''
                continue
            if t2 != 60:                                      # indexed_trilist
                continue
            vi = _child(d, ps, pe, 63)                        # vertex_idxs
            k = _body(d, vi[0])
            m, = struct.unpack_from('<I', d, k)
            idx = struct.unpack_from('<%dI' % m, d, k + 4)
            for a in range(0, m - 2, 3):
                vv = [V[idx[a + c]] for c in range(3)]
                tris.append((tuple(P[v[0]] for v in vv),
                             tuple(UV[v[1]] if v[1] < len(UV) else (0, 0) for v in vv), state))
    return tris


_TEX = {}


def texture(dirs, name, tsre):
    """A texture as a numpy RGB array (rows from the top), decoded with
    TSRE's ACE converter; None when not found."""
    import subprocess
    import tempfile
    import numpy as np
    key = name.lower()
    if key in _TEX:
        return _TEX[key]
    found = None
    for d in dirs:
        if os.path.isdir(d):
            for f in os.listdir(d):
                if f.lower() == key:
                    found = os.path.join(d, f)
                    break
        if found:
            break
    arr = None
    if found:
        with tempfile.TemporaryDirectory() as tmp:
            png = os.path.join(tmp, 't.png')
            env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
            subprocess.run([tsre, '--aceconv', '--file', found, '--output', png, '--overwrite'],
                           env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
            if os.path.exists(png):
                raw = subprocess.run(['magick', png, '-alpha', 'off', '-depth', '8', 'rgb:-'],
                                     capture_output=True, check=True).stdout
                w, h = map(int, subprocess.run(['magick', 'identify', '-format', '%w %h', png],
                                               capture_output=True, text=True).stdout.split())
                arr = np.frombuffer(raw, np.uint8).reshape(h, w, 3)
    _TEX[key] = arr
    return arr


def rasterize(tris, box, sc, tex_dirs, tsre):
    """Textured triangles seen from above into an RGB image of the box
    (x0, x1, z0, z1) at sc pixels per metre; lower triangles first."""
    import numpy as np
    x0, x1, z0, z1 = box
    W, H = int((x1 - x0) * sc), int((z1 - z0) * sc)
    img = np.full((H, W, 3), (244, 241, 234), np.uint8)
    for pts, uvs, image, _ in sorted(tris, key=lambda t: t[3]):
        tex = texture(tex_dirs, image, tsre) if image else None
        X = [(p[0] - x0) * sc for p in pts]
        Y = [(z1 - p[1]) * sc for p in pts]
        xa, xb = max(int(min(X)), 0), min(int(max(X)) + 1, W)
        ya, yb = max(int(min(Y)), 0), min(int(max(Y)) + 1, H)
        if xa >= xb or ya >= yb:
            continue
        den = (Y[1] - Y[2]) * (X[0] - X[2]) + (X[2] - X[1]) * (Y[0] - Y[2])
        if abs(den) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(xa, xb) + 0.5, np.arange(ya, yb) + 0.5)
        l0 = ((Y[1] - Y[2]) * (gx - X[2]) + (X[2] - X[1]) * (gy - Y[2])) / den
        l1 = ((Y[2] - Y[0]) * (gx - X[2]) + (X[0] - X[2]) * (gy - Y[2])) / den
        l2 = 1 - l0 - l1
        inside = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        if not inside.any():
            continue
        if tex is None:
            img[ya:yb, xa:xb][inside] = (120, 120, 120)
            continue
        u = l0 * uvs[0][0] + l1 * uvs[1][0] + l2 * uvs[2][0]
        v = l0 * uvs[0][1] + l1 * uvs[1][1] + l2 * uvs[2][1]
        th, tw = tex.shape[:2]
        tu = (np.floor(u * tw).astype(int)) % tw
        tv = (np.floor(v * th).astype(int)) % th
        img[ya:yb, xa:xb][inside] = tex[tv[inside], tu[inside]]
    return img


def png_data_uri(img):
    import base64
    import subprocess
    h, w = img.shape[:2]
    ppm = b'P6 %d %d 255\n' % (w, h) + img.tobytes()
    png = subprocess.run(['magick', 'ppm:-', 'png:-'], input=ppm, capture_output=True, check=True).stdout
    return 'data:image/png;base64,' + base64.b64encode(png).decode()


def find_shape(dirs, name):
    name = name.replace('\\', '/')
    for d in dirs:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None


# --- plans ----------------------------------------------------------------------

def with_included(shape, shapes_by_name, included):
    """The shape with the included track shapes' paths added, each placed at
    (x, y, z, rotY) in the shape's space (rotY degrees, positive right, as
    SectionIdx). Returns the shape and the number of its own paths."""
    s = dict(shape)
    s['paths'] = list(shape['paths'])
    for name, x, y, z, roty in included:
        other = shapes_by_name[name.lower()]
        r = math.radians(roty)
        for p in other['paths']:
            px, py, pz = p['pos']
            s['paths'].append({'pos': (x + px * math.cos(r) + pz * math.sin(r), y + py,
                                       z - px * math.sin(r) + pz * math.cos(r)),
                               'rot': p['rot'] + roty, 'sect': p['sect']})
    return s, len(shape['paths'])


def plan_object(shape, secs, excluded=(), included=(), shapes_by_name=None,
                included_rails=False):
    own = len(shape['paths'])
    if included:
        shape, own = with_included(shape, shapes_by_name, included)
    paths, rails, ties, _ = T.plan(shape, secs)
    if not included_rails:
        rails = rails[:own]
    if 'rail' in excluded:
        rails = []
    if 'tie' in excluded:
        ties = []
    elif 'fastener' in excluded:
        ties = [dict(t, fast=[]) for t in ties]
    return rails, ties


# --- drawing --------------------------------------------------------------------

def panel(title, items, box, sc, ox):
    x0, x1, z0, z1 = box
    def P(x, z):
        return f'{(x - x0) * sc + ox:.1f},{(z1 - z) * sc + 40:.1f}'
    w = (x1 - x0) * sc
    o = [f'<rect x="{ox}" y="0" width="{w:.0f}" height="{(z1 - z0) * sc + 60:.0f}" fill="#f4f1ea"/>',
         f'<text x="{ox + 8}" y="22" font-family="sans-serif" font-size="15" fill="#222">{title}</text>']
    for kind, pts, style in items:
        if kind == 'poly':
            o.append(f'<polygon points="{" ".join(P(*q) for q in pts)}" {style}/>')
        elif kind == 'line':
            o.append(f'<polyline points="{" ".join(P(*q) for q in pts)}" fill="none" {style}/>')
        elif kind == 'dot':
            c = P(*pts).split(',')
            o.append(f'<circle cx="{c[0]}" cy="{c[1]}" r="1.5" {style}/>')
    return o, w


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tsection')
    ap.add_argument('wfile')
    ap.add_argument('uid', type=int)
    ap.add_argument('out')
    ap.add_argument('--radius', type=float, default=20)
    ap.add_argument('--scale', type=float, default=18.0, help='pixels per metre')
    ap.add_argument('--uids', type=int, nargs='*', default=[],
                    help='the objects to draw (else all within --radius)')
    ap.add_argument('--textures', nargs='*', default=[], help='TEXTURES folders for the static panel')
    ap.add_argument('--tsre', default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                    '..', '..', 'build', 'TSRE5vc'))
    ap.add_argument('--shapes', nargs='*', default=[])
    ap.add_argument('--excluded-parts', nargs=2, action='append', default=[],
                    metavar=('PATTERN', 'PARTS'))
    ap.add_argument('--included-track-shape', nargs=6, action='append', default=[],
                    metavar=('PATTERN', 'SHAPE', 'X', 'Y', 'Z', 'ROTY'))
    ap.add_argument('--included-rails', action='store_true',
                    help="the host draws the included shapes' rails too")
    args = ap.parse_args()
    secs, shapes = parse(args.tsection)
    by_name = {s.get('file', '').lower(): s for s in shapes.values()}
    objs = track_objects(args.wfile)
    centre = next(o for o in objs if o['uid'] == args.uid)
    if args.uids:
        group = [o for o in objs if o['uid'] in set(args.uids) | {args.uid}]
    else:
        group = [o for o in objs if math.hypot(o['pos'][0] - centre['pos'][0],
                                               o['pos'][2] - centre['pos'][2]) <= args.radius]

    def match(o, pat):
        return fnmatch.fnmatch(o['file'].replace('\\', '/').lower(), pat.lower())

    def local(o, x, z):
        return to_local(centre, *to_world(o, x, z))

    static, filt, plain = [], [], []
    rail_style = 'stroke="#202020" stroke-width="1.6"'
    tie_style = {'own': 'fill="#8a6a45" stroke="#222" stroke-width="0.5"',
                 'tie': 'fill="#6e5a44" stroke="#222" stroke-width="0.5"',
                 'bearer': 'fill="#3f6fa8" stroke="#222" stroke-width="0.5"'}
    allpts = []
    for o in group:
        sh = shapes[o['sect']]
        # static
        path = find_shape(args.shapes, o['file']) if args.shapes else None
        tris = shape_triangles(path) if path else None
        for pts, uvs, image in tris or []:
            static.append(([local(o, p[0], p[2]) for p in pts], uvs, image, sum(p[1] for p in pts) / 3))
        # plans
        excl = {x.lower() for e in args.excluded_parts if match(o, e[0]) for x in e[1].split(',')}
        incl = [(e[1], float(e[2]), float(e[3]), float(e[4]), float(e[5]))
                for e in args.included_track_shape if match(o, e[0])]
        for target, ex, inc in ((filt, excl, incl), (plain, set(), [])):
            rails, ties = plan_object(sh, secs, ex, inc, by_name, args.included_rails)
            for t in ties:
                target.append(('poly', [local(o, *q) for q in t['r']], tie_style[t['kind']]))
                for f in t.get('fast', []):
                    target.append(('dot', local(o, *f), 'fill="#d03030"'))
            for l, r in rails:
                for rl in (l, r):
                    pts = [local(o, q[0], q[1]) for q in rl[::2]]
                    allpts += pts
                    target.append(('line', pts, rail_style))
    xs = [p[0] for p in allpts]; zs = [p[1] for p in allpts]
    box = (min(xs) - 4, max(xs) + 4, min(zs) - 3, max(zs) + 3)
    sc = args.scale
    parts, ox = [], 0
    names = ', '.join(sorted({os.path.basename(o['file'].replace('\\', '/')) for o in group}))
    if static:
        img = rasterize(static, box, sc * 2, args.textures, args.tsre)
        w = (box[1] - box[0]) * sc
        h = (box[3] - box[2]) * sc
        parts += [f'<rect x="0" y="0" width="{w:.0f}" height="{h + 60:.0f}" fill="#f4f1ea"/>',
                  f'<image x="0" y="40" width="{w:.0f}" height="{h:.0f}" href="{png_data_uri(img)}"/>',
                  '<text x="8" y="22" font-family="sans-serif" font-size="15" fill="#222">static shapes (textured, from above)</text>']
        ox += w + 12
    for title, items in (('with the filter values', filt), ('every object its own ties', plain)):
        o, w = panel(title, items, box, sc, ox)
        parts += o
        ox += w + 12
    H = (box[3] - box[2]) * sc + 60
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{ox:.0f}" height="{H + 24:.0f}">',
           f'<rect width="100%" height="100%" fill="#ffffff"/>'] + parts + [
           f'<text x="8" y="{H + 16:.0f}" font-family="sans-serif" font-size="12" fill="#444">UiD {args.uid} and neighbours: {names}</text>',
           '</svg>']
    open(args.out, 'w').write('\n'.join(svg))
    print(args.out, len(group), 'objects')


if __name__ == '__main__':
    main()
