"""Read an MSTS tsection.dat (UTF-16 or UTF-8 text): TrackSections and
TrackShapes, and sample a shape's paths in shape space.

Shape space is MSTS's: +x right, +z forward, headings about y in radians,
positive to the right. SectionCurve angles are positive to the right too
(A1tPnt5dLft's diverging curve is -5). SectionIdx paths are listed from the
leftmost to the rightmost."""
import math
import re

TOKEN = re.compile(r'\(|\)|"[^"]*"|[^\s()]+')


def read_text(path):
    data = open(path, 'rb').read()
    if data[:2] in (b'\xff\xfe', b'\xfe\xff'):
        return data.decode('utf-16')
    return data.decode('utf-8', errors='replace')


def _block(toks, i):
    """toks[i] == '(': the block's items (words and (name, items) pairs)
    and the index after its ')'."""
    i += 1
    out = []
    while i < len(toks) and toks[i] != ')':
        if i + 1 < len(toks) and toks[i + 1] == '(':
            name = toks[i]
            sub, i = _block(toks, i + 1)
            out.append((name, sub))
        elif toks[i] == '(':
            sub, i = _block(toks, i)
            out.append(('', sub))
        else:
            out.append(toks[i])
            i += 1
    return out, i + 1


def parse(path):
    """(sections, shapes): sections {idx: {'len'} or {'radius', 'angle'}},
    shapes {idx: {'file', 'numpaths', 'mainroute', 'clearance', 'paths',
    'xover', 'flags', 'info'}} ('info': the _INFO lines above the shape's
    run); a path is {'pos': (x, y, z), 'rot': degrees,
    'sect': [section indices]}."""
    toks = TOKEN.findall(read_text(path))
    sections, shapes = {}, {}
    i = 0
    while i < len(toks):
        if i + 1 < len(toks) and toks[i + 1] == '(':
            name = toks[i].lower()
            sub, i = _block(toks, i + 1)
            if name == 'tracksections':
                for it in sub:
                    if isinstance(it, tuple) and it[0].lower() == 'tracksection':
                        b = it[1]
                        d = {'idx': int(b[0])}
                        for x in b[1:]:
                            if not isinstance(x, tuple):
                                continue
                            k = x[0].lower()
                            if k == 'sectionsize':
                                d['gauge'], d['len'] = float(x[1][0]), float(x[1][1])
                            elif k == 'sectioncurve':
                                d['radius'], d['angle'] = float(x[1][0]), float(x[1][1])
                        sections[d['idx']] = d
            elif name == 'trackshapes':
                # _INFO lines above a run of shapes (set names, credits):
                # each shape gets the latest run of them
                header, after_shape = [], False
                for it in sub:
                    if isinstance(it, tuple) and it[0].lower() == '_info':
                        if after_shape:
                            header, after_shape = [], False
                        header.append(' '.join(w for w in it[1] if isinstance(w, str)).strip('" '))
                        continue
                    if isinstance(it, tuple) and it[0].lower() == 'trackshape':
                        after_shape = True
                        b = it[1]
                        d = {'idx': int(b[0]), 'paths': [], 'flags': [], 'info': list(header)}
                        for x in b[1:]:
                            if not isinstance(x, tuple):
                                continue
                            k, v = x[0].lower(), x[1]
                            if k == 'filename':
                                d['file'] = v[0]
                            elif k == 'numpaths':
                                d['numpaths'] = int(v[0])
                            elif k == 'mainroute':
                                d['mainroute'] = int(v[0])
                            elif k == 'clearancedist':
                                d['clearance'] = float(v[0])
                            elif k == 'sectionidx':
                                n = int(v[0])
                                d['paths'].append({
                                    'pos': tuple(map(float, v[1:4])), 'rot': float(v[4]),
                                    'sect': [int(float(s)) for s in v[5:5 + n]]})
                            elif k == 'xoverpts':
                                d['xover'] = v
                            else:
                                d['flags'].append(k)
                        shapes[d['idx']] = d
        else:
            i += 1
    return sections, shapes


def sample(path, sections, step=0.25):
    """A path's centre line: [(x, z, heading, distance)], None when a
    section is missing."""
    x, _, z = path['pos']
    h = math.radians(path['rot'])
    s = 0.0
    pts = [(x, z, h, 0.0)]
    for idx in path['sect']:
        d = sections.get(idx)
        if d is None:
            return None
        if 'radius' in d:
            a = math.radians(d['angle'])
            length = abs(a) * d['radius']
        else:
            a, length = 0.0, d.get('len', 0.0)
        n = max(1, int(math.ceil(length / step)))
        for _ in range(n):
            seg, da = length / n, a / n
            hm = h + da / 2
            x += seg * math.sin(hm)
            z += seg * math.cos(hm)
            h += da
            s += seg
            pts.append((x, z, h, s))
    return pts


def offset(pts, d):
    """The line d metres to the right of a sampled path."""
    return [(x + d * math.cos(h), z - d * math.sin(h), h, s) for x, z, h, s in pts]
