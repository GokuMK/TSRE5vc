#!/usr/bin/env python3
"""Sitting places and doors of cars for TSRE's people (ESD_TSRESeats in a
model's .sd): the car's Aisle, its Passages (partitions' closed doors across
the aisle), its numbered Seats and its numbered Doors, found in the model's
most detailed level (an MSTS .s through TSRE's converter, a MaSzyna .e3d by
its records), in the model's space as written (an S file's as its file's,
as the lights' positions). TSRE never guesses seats at load: a car has them
once its .sd has them; its doors it takes from the .sd when written there,
else from its door parts at load. The workflow: README.md beside this
script.

  scan:  car_seats.py scan <folder> ... --out <dir> [--maszyna]   the cars of
         vehicle folders (.eng/.wag shapes and freight animations; with
         --maszyna .mmd models and interiors): report.md, found.json, plans
  find:  car_seats.py find <model> ... [--json out.json]           a summary
  plan:  car_seats.py plan <model> ... --out <dir> [--sd] [--root <dir>]
         top views (SVG): the seats found, or with --sd the model's .sd as
         written; named by the model's path under --root when given
  write: car_seats.py write <model> ... --root <game root> --archive <prefix>
         [--min-seats 8] [--readme README.md] [--plans <dir>] [--stage <dir>]
  doors: car_seats.py doors <model> ... --root <game root> --archive <prefix>
         [--plans <dir>] [--stage <dir>]      only the Door lines, its other
                                              lines kept (a block made)
  drop:  car_seats.py drop <model> <number> ... --root <game root>
         --archive <prefix> [--stage <dir>]   wrong seats (12) or doors (d2)
                                              out of its .sd
  aisles: car_seats.py aisles <model> "x y z0 z1" ... --root <game root>
         --archive <prefix> [--stage <dir>]   its Aisle lines replaced by
                                              these (numbered A1, A2...),
                                              its other lines kept
  archive: car_seats.py archive --stage <dir> --root <game root> --archive
         <prefix> [--readme README.md]        the staged set after its
                                              steps: one zip to apply, its
                                              backup, NEW-SD.txt

  Every command takes --tsre <TSRE5vc executable> (default: the build
  folder of the checkout this script is in) for .s files.

How a car is read: the floor is the height with the most upward area inside
the body; cushions are upward faces 0.33 to 0.58 m above it, grouped into
benches (one side of the aisle, next to each other along the car); a
bench's back is the side its backrest (upright faces above the cushion)
leans to, unless that side has nothing of a backrest's height and the
other has; the sitter faces away from it. A backrest standing inside a
bench across the car parts it: two benches back to back, each facing away
(or a panel behind one, too shallow to keep). Its places: one a cushion
piece up to 0.8 m wide (armchairs, single seats), else 0.45 m apart; none
within 0.3 m of the aisle's line (flip seats, ledges), none in a cab (a
room at the car's end behind a partition across the car, 0.9 to 3 m from
the model's end, with 3 places at most). The aisle runs between the
benches' inner edges, along the floor around the car's middle; a
partition is a wall across the car blocking the aisle (its door modelled
closed): a Passage. Parts named for a cab and models named for one are
left out. Doors as TSRE finds them at load (CarDoors): an S file's parts
DOOR_A..DOOR_F and those under them, each triangle on the side of the car
it is on (its left at -x as written; a part may hold both sides' leaves),
triangles at least 1 m off the car's middle (a roof's hatch left out),
each side's split along the car where their spans leave more than 0.35 m
between them, pieces 0.4 to 4.5 m wide with their outer face at least
1.2 m off the middle (inner doors left out); a door's x is its points' mean
(its outer face), y its lowest point (the floor at the threshold), z its
middle. MaSzyna models' doors are not written: TSRE reads them by the
.mmd's door prefix.

write: each model's .sd beside it, its other lines kept and an
ESD_TSRESeats block put in (a new .sd: the route's header, ESD_Detail_Level
and ESD_Alternative_Texture 0; beside an .e3d only the block); UTF-16 with
BOM and CRLF, as the route's. Seats numbered from 1 (`Seat 12 ( x y z
heading )`), as on the plans. Before writing, <archive>-backup.zip holds the
.sd files there were; after, <archive>.zip the written ones; both by their
paths under --root, to extract into it. Cars with fewer seats than
--min-seats get no seats (their doors only, if any). Doors numbered from 1
(`Door 2 ( x y z width )`), as on the plans.

--stage <dir>: nothing written into the content; each .sd goes to <dir> by
its path under --root (a staged .sd is read before the content's, so drop
and plan --sd work on it); <archive>-backup.zip still holds the content's
.sd files as they are, <archive>.zip the staged ones (each step's own:
`archive` makes the set's from the staging folder at the end).
"""
import argparse, collections, glob, json, math, multiprocessing, os, re, struct, subprocess, sys, tempfile, zipfile

PLACE = 0.45          # a place's width on a bench (m)
CUSHION = (0.33, 0.58)  # cushion tops above the floor (m)
CUSHION_USUAL = 0.45    # a cushion top's usual height (m): which floor's
# Task humans 11's rules (the mapping of OR_PSK_NEW and MaSzyna, its
# report's numbers): a backrest within BACK_AT of a cushion's edge stands at
# it, further in it parts the cushion (two benches back to back, or a panel
# behind one), each side a bench when at least MIN_DEPTH deep (benches back
# to back are 0.5 m each, a panel behind one shallower; a whole cushion
# needs 0.25 m as before: dining chairs' are 0.28); places nearer the
# aisle's line than OFF_AISLE are flip seats and ledges; cushion pieces up
# to PIECE wide are one place each (armchairs); a room at a car's end behind
# a partition across the car, END_ROOM deep from the model's end (buffers
# included), with CAB_PLACES places at most, is a cab (more: an end
# compartment, EW60's); door parts' triangles nearer the car's middle than
# DOOR_OFF_MIDDLE are not a door's (a roof's hatch), and doors whose outer
# face is nearer it than DOOR_SIDE are inner ones (all OR_PSK_NEW's real
# doors at 1.26 m or more, CD Bdmpee's saloon doors at 1.10).
BACK_AT = 0.15
MIN_DEPTH = 0.35
OFF_AISLE = 0.3
PIECE = (0.2, 0.8)
END_ROOM = (0.9, 3.0)
CAB_PLACES = 3
DOOR_OFF_MIDDLE = 1.0
DOOR_SIDE = 1.2
HEADER = "SIMISA@@@@@@@@@@JINX0t1t______"
# Parts of a cab (MSTS matrices, MaSzyna parts): no passengers' seats there.
DOOR_PART = re.compile(r"^DOOR_([A-F])", re.I)
CAB = re.compile(r"cab|kabin|maszynist|mechanik|driver|fuehrer|f\u00fchrer", re.I)
HERE = os.path.dirname(os.path.abspath(__file__))
TSRE = os.path.normpath(os.path.join(HERE, "..", "..", "build", "TSRE5vc"))


# --- S file text (as scripts/lamp_shapes.py) ----------------------------------
def to_text(tsre, shape, out):
    """A shape as uncompressed unicode text, through TSRE's converter."""
    subprocess.run([tsre, "--shapeconv", "-i", shape, "-o", out, "-f", "unicode", "-c", "uncompressed", "-w"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def tree(text):
    root, stack = [], []
    cur = root
    for tok in re.findall(r'\(|\)|"[^"]*"|[^\s()]+', text):
        if tok == "(":
            new = []
            cur.append(new)
            stack.append(cur)
            cur = new
        elif tok == ")":
            if stack:  # a vehicle file's extra ")" passed over
                cur = stack.pop()
        else:
            cur.append(tok)
    return root


def blocks(lst, name):
    """(label, block) for each `name [label] ( ... )` directly in lst."""
    out = []
    for i, item in enumerate(lst):
        if not isinstance(item, list):
            continue
        j, label = i - 1, None
        if j >= 1 and isinstance(lst[j], str) and lst[j] != name and lst[j - 1] == name:
            label, j = lst[j], j - 1
        if j >= 0 and lst[j] == name:
            out.append((label, item))
    return out


def first(lst, name):
    b = blocks(lst, name)
    return b[0][1] if b else None


def find_block(lst, name):
    b = first(lst, name)
    if b is not None:
        return b
    for item in lst:
        if isinstance(item, list):
            r = find_block(item, name)
            if r is not None:
                return r
    return None


def mul(p, m):
    # MSTS row vector: p * R + t; m = R's three rows, then t.
    return [p[0] * m[0] + p[1] * m[3] + p[2] * m[6] + m[9],
            p[0] * m[1] + p[1] * m[4] + p[2] * m[7] + m[10],
            p[0] * m[2] + p[1] * m[5] + p[2] * m[8] + m[11]]


# --- Triangles ---------------------------------------------------------------
def msts_triangles(path, tsre):
    """The most detailed level's triangles of an .s, in its file's space
    (through each matrix up to, not through, the root), with the matrix's
    name."""
    with tempfile.TemporaryDirectory() as tmp:
        text_path = os.path.join(tmp, "shape.s")
        to_text(tsre, path, text_path)
        shape = find_block(tree(open(text_path, "rb").read().decode("utf-16")), "shape")
    points = [[float(v) for v in b] for _, b in blocks(first(shape, "points"), "point")]
    matrices = blocks(first(shape, "matrices"), "matrix")
    names = [label for label, _ in matrices]
    mats = [[float(v) for v in b] for _, b in matrices]
    vtx_states = [[int(v) for v in b[:5]] for _, b in blocks(first(shape, "vtx_states"), "vtx_state")]
    prim_states = []
    for label, b in blocks(first(shape, "prim_states"), "prim_state"):
        tex = first(b, "tex_idxs")
        rest = [x for x in b[b.index(tex) + 1:] if isinstance(x, str)]
        prim_states.append(int(rest[1]))
    level = blocks(find_block(shape, "distance_levels"), "distance_level")[0][1]
    hierarchy = [int(v) for v in first(first(level, "distance_level_header"), "hierarchy")[1:]]

    def to_root(p, m):
        while m > 0:
            p = mul(p, mats[m])
            m = hierarchy[m]
        return p

    def named(m):
        """A matrix's name; under a door part, `DOOR_x/<its name>`."""
        up = m
        while up >= 0:
            if DOOR_PART.match(names[up]):
                return names[m] if up == m else names[up] + "/" + names[m]
            up = hierarchy[up] if up > 0 else -1
        return names[m]
    out = []
    for _, sub in blocks(first(level, "sub_objects"), "sub_object"):
        verts = [int(b[1]) for _, b in blocks(first(sub, "vertices"), "vertex")]
        prims = first(sub, "primitives")
        state = None
        for i, item in enumerate(prims):
            if item == "prim_state_idx":
                state = int(prims[i + 1][0])
            elif item == "indexed_trilist":
                matrix = vtx_states[prim_states[state]][1]
                idx = [int(v) for v in first(prims[i + 1], "vertex_idxs")[1:]]
                for k in range(0, len(idx) - 2, 3):
                    out.append(([to_root(points[verts[v]], matrix) for v in idx[k:k + 3]], named(matrix)))
    return out


def e3d_chunks(b, off, end):
    while off + 8 <= end:
        cid, size = b[off:off + 4].decode("latin-1"), struct.unpack("<I", b[off + 4:off + 8])[0]
        if size < 8:
            break
        yield cid, off, size
        off += size


def e3d_triangles(path):
    """A MaSzyna .e3d's triangles in its model's space (records SUB0/SUB1,
    vertices VNT0/VNT1/VNT2, indices IDX1/2/4, matrices TRA0), with the
    part's name."""
    b = open(path, "rb").read()
    ch = {}
    for cid, off, size in e3d_chunks(b, 0, len(b)):
        if cid == "E3D0":
            for c2, o2, s2 in e3d_chunks(b, off + 8, off + size):
                ch[c2] = b[o2 + 8:o2 + s2]
    sub, size = (ch["SUB0"], 256) if "SUB0" in ch else (ch.get("SUB1", b""), 320)
    recs = []
    for i in range(len(sub) // size):
        r = sub[i * size:(i + 1) * size]
        g = lambda at, f="<i": struct.unpack(f, r[at:at + 4])[0]  # noqa: E731
        recs.append({"next": g(0), "child": g(4), "type": g(8), "name": g(12), "flags": g(20, "<I"), "matrix": g(24),
                     "vc": g(28), "vo": g(32), "ic": g(156), "io": g(160)})
    names = [x.decode("cp1250", "replace") for x in ch.get("NAM0", b"").split(b"\0")]
    if "VNT2" in ch:
        vb, stride = ch["VNT2"], 48
    else:
        vb, stride = ch.get("VNT0", ch.get("VNT1", b"")), 32
    pos = [struct.unpack_from("<3f", vb, k * stride) for k in range(len(vb) // stride)]
    if "IDX2" in ch:
        idx = struct.unpack("<%dH" % (len(ch["IDX2"]) // 2), ch["IDX2"])
    elif "IDX4" in ch:
        idx = struct.unpack("<%dI" % (len(ch["IDX4"]) // 4), ch["IDX4"])
    elif "IDX1" in ch:
        idx = tuple(ch["IDX1"])
    else:
        idx = None
    tra = ch.get("TRA0", b"")
    mats = [struct.unpack_from("<16f", tra, k * 64) for k in range(len(tra) // 64)]

    def apply(m, p):
        return (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
                m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14])
    out = []

    def walk(i, chain, depth):
        while 0 <= i < len(recs) and depth < 64:
            r = recs[i]
            here = chain + ([mats[r["matrix"]]] if (r["flags"] & 0x8000) and 0 <= r["matrix"] < len(mats) else [])
            name = names[r["name"]] if 0 <= r["name"] < len(names) else ""
            if r["type"] == 4 and r["vc"] > 0:
                order = idx[r["io"]:r["io"] + r["ic"]] if idx is not None and r["ic"] > 0 else range(r["vc"])

                def at(v):
                    p = pos[r["vo"] + v]
                    for m in reversed(here):
                        p = apply(m, p)
                    return p
                for k in range(0, len(order) - 2, 3):
                    out.append(([at(order[k]), at(order[k + 1]), at(order[k + 2])], name))
            if r["child"] > 0:
                walk(r["child"], here, depth + 1)
            i = r["next"] if r["next"] > 0 else -1
    walk(0, [], 0)
    return out


def triangles(path, tsre):
    if path.lower().endswith(".e3d"):
        return e3d_triangles(path)
    return msts_triangles(path, tsre)


# --- Finding -----------------------------------------------------------------
def normal_area(t):
    a = [t[1][i] - t[0][i] for i in range(3)]
    b = [t[2][i] - t[0][i] for i in range(3)]
    n = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    length = math.sqrt(sum(x * x for x in n))
    return ([x / length for x in n] if length > 0 else [0.0, 0.0, 0.0]), length / 2


def percentile(values, q):
    s = sorted(values)
    return s[int(q * (len(s) - 1))]


# Rule 4 of task humans 11 (2+1 seating swapping sides at a vestibule, ED160
# b): benches further apart along the car than SALOON_GAP are another
# saloon; a saloon's aisle is the widest gap across the car between its
# cushions, AISLE_LEAST wide or more; saloons whose aisles lie more than
# SALOON_SHIFT apart get an aisle each, neighbours overlapping by
# SALOON_OVERLAP at the middle of the gap between them (their join: TSRE
# finds it where they overlap).
SALOON_GAP = 1.0
AISLE_LEAST = 0.4
SALOON_SHIFT = 0.25
SALOON_OVERLAP = 0.25


def saloon_pieces(benches):
    """Where a car's saloons have aisles of their own across the car (rule
    4): [[x, width, z0, z1]] along the car, z0 of the first and z1 of the
    last unbounded, the others at the middle of the gaps between the
    saloons (a saloon without an aisle of its own: its neighbours'); None
    where the car's saloons share one line (within SALOON_SHIFT)."""
    groups = []
    for b in sorted(benches, key=lambda b: b["z0"]):
        if groups and b["z0"] <= groups[-1][1] + SALOON_GAP:
            groups[-1][1] = max(groups[-1][1], b["z1"])
            groups[-1][2].append(b)
        else:
            groups.append([b["z0"], b["z1"], [b]])
    found = []  # [x, width, z0, z1, area]
    for z0, z1, bs in groups:
        merged = []
        for lo, hi in sorted((min(p[0] for p in t), max(p[0] for p in t)) for b in bs for t, _ in b["faces"]):
            if merged and lo <= merged[-1][1] + 0.05:
                merged[-1][1] = max(merged[-1][1], hi)
            else:
                merged.append([lo, hi])
        gaps = [(merged[i + 1][0] - merged[i][1], (merged[i][1] + merged[i + 1][0]) / 2) for i in range(len(merged) - 1)]
        gaps = [g for g in gaps if g[0] >= AISLE_LEAST]
        if gaps:
            w, x = max(gaps)
            found.append([x, w, z0, z1, sum(b["area"] for b in bs)])
    if len(found) < 2 or max(f[0] for f in found) - min(f[0] for f in found) <= SALOON_SHIFT:
        return None
    # Neighbours at about one x: one piece, at the larger saloon's.
    pieces = []
    for f in found:
        p = pieces[-1] if pieces else None
        if p and abs(p[0] - f[0]) <= SALOON_SHIFT:
            if f[4] > p[5]:
                p[0], p[1], p[5] = f[0], f[1], f[4]
            p[3] = f[3]
        else:
            pieces.append(f[:4] + [f[4], f[4]])
    if len(pieces) < 2:
        return None
    out = []
    for i, p in enumerate(pieces):
        z0 = -math.inf if i == 0 else (pieces[i - 1][3] + p[2]) / 2
        z1 = math.inf if i == len(pieces) - 1 else (p[3] + pieces[i + 1][2]) / 2
        out.append([p[0], p[1], z0, z1])
    return out


def on_saloons(saloons, y, spans):
    """Aisles at floor height y along stretches of the car: each saloon's
    line within its part of them, neighbours overlapping by SALOON_OVERLAP
    (one saloon: a stretch's aisle as it is)."""
    out = []
    for z0, z1 in spans:
        for x, w, a0, a1 in saloons:
            lo, hi = max(z0, a0 - SALOON_OVERLAP), min(z1, a1 + SALOON_OVERLAP)
            if hi - lo >= 0.25:
                out.append([round(x, 3), round(y, 3), round(lo, 3), round(hi, 3)])
    return out


def _find(tris, floor=None, spans=None, aisle_at=None, exclude=()):
    """The car's floor, aisle, passages and seats (its model's space). On a
    floor level of its own (find()): its floor height, the stretches along
    the car it covers the aisle's line in (spans: its cushions only there),
    the main floor's aisle line (its own where it has benches either side)
    and the other levels' heights (faces there are floors, no cushions)."""
    if not tris:
        return {"problem": "no triangles"}
    xs = [p[0] for t, _ in tris for p in t]
    zs = [p[2] for t, _ in tris for p in t]
    half = max(abs(percentile(xs, 0.02)), abs(percentile(xs, 0.98)))
    z0, z1 = percentile(zs, 0.01), percentile(zs, 0.99)

    def inside(p, margin):
        return abs(p[0]) < half - margin and z0 + 0.3 < p[2] < z1 - 0.3
    faces = []
    for t, part in tris:
        n, a = normal_area(t)
        if a > 0:
            faces.append((t, part, n, a, [sum(p[i] for p in t) / 3 for i in range(3)]))
    # The floor: upward faces counted by their middle (a floor of one plate
    # reaching past the inside: all of it left out, the seats' tops were
    # taken for the floor).
    if floor is None:
        floors = collections.Counter()
        for t, part, n, a, c in faces:
            if n[1] > 0.95 and 0.6 < c[1] < 1.8 and inside(c, 0.12):
                floors[round(c[1], 2)] += a
        if not floors:
            return {"problem": "no floor"}
        floor = max(floors, key=floors.get)

    def on_spans(z, margin):
        return spans is None or any(z0_ - margin <= z <= z1_ + margin for z0_, z1_ in spans)

    def other_floor(t):
        return any(all(abs(p[1] - h) < 0.03 for p in t) for h in exclude)

    def others_cushion(y):
        # A cushion at a height a cushion has over another floor too, nearer
        # its usual height there: that floor's (a low entrance's "seats"
        # are the saloon's benches by it).
        mine = abs(y - floor - CUSHION_USUAL)
        return any(CUSHION[0] < y - h < CUSHION[1] and abs(y - h - CUSHION_USUAL) < mine for h in exclude)

    # Cushions, grouped by side and along the car into benches.
    benches = []
    for t, part, n, a, c in faces:
        y = c[1] - floor
        if CAB.search(part or ""):
            continue  # a cab's seats are the crew's
        if n[1] > 0.85 and CUSHION[0] < y < CUSHION[1] and a > 0.002 and inside(c, 0.04) \
                and on_spans(c[2], 0.3) and not other_floor(t) and not others_cushion(c[1]):
            side = c[0] > 0
            for b in benches:
                if b["side"] == side and abs(b["y"] - y) < 0.08 and b["z0"] - 0.12 < c[2] < b["z1"] + 0.12:
                    b["z0"] = min(b["z0"], min(p[2] for p in t))
                    b["z1"] = max(b["z1"], max(p[2] for p in t))
                    b["x0"] = min(b["x0"], min(p[0] for p in t))
                    b["x1"] = max(b["x1"], max(p[0] for p in t))
                    b["area"] += a
                    b["ys"].append((y, a))
                    b["faces"].append((t, c))
                    break
            else:
                benches.append({"side": side, "y": y, "z0": min(p[2] for p in t), "z1": max(p[2] for p in t),
                                "x0": min(p[0] for p in t), "x1": max(p[0] for p in t), "area": a, "ys": [(y, a)],
                                "faces": [(t, c)]})
    benches = [b for b in benches if b["area"] > 0.1 and b["z1"] - b["z0"] > 0.25 and b["x1"] - b["x0"] > 0.25]
    if not benches:
        return {"problem": "no cushions", "floor": floor}
    # The car's cushions are of one height: others (a cab's seats, higher)
    # are not the passengers'.
    for b in benches:
        b["y"] = sum(y * a for y, a in b["ys"]) / sum(a for _, a in b["ys"])
    # One bench found twice (a cushion and a layer over it, a little higher
    # or lower): the upper one kept.
    kept = []
    for b in sorted(benches, key=lambda b: -b["y"]):
        def overlap(lo0, hi0, lo1, hi1):
            return max(0.0, min(hi0, hi1) - max(lo0, lo1)) / max(1e-6, min(hi0 - lo0, hi1 - lo1))
        if any(k["side"] == b["side"] and overlap(b["z0"], b["z1"], k["z0"], k["z1"]) > 0.5
               and overlap(b["x0"], b["x1"], k["x0"], k["x1"]) > 0.5 for k in kept):
            continue
        kept.append(b)
    benches = kept
    usual = percentile([b["y"] for b in benches], 0.5)
    benches = [b for b in benches if abs(b["y"] - usual) < 0.05]
    plus = [b for b in benches if b["side"]]
    minus = [b for b in benches if not b["side"]]
    if (not plus or not minus) and aisle_at is None:
        return {"problem": "seats on one side only", "floor": floor}
    if plus and minus:
        inner_plus = percentile([b["x0"] for b in plus], 0.2)
        inner_minus = percentile([b["x1"] for b in minus], 0.8)
        aisle_x = (inner_plus + inner_minus) / 2
        aisle_width = inner_plus - inner_minus
    else:
        aisle_x, aisle_width = aisle_at, 0.6
    if aisle_width < 0.4:
        if aisle_at is None:
            return {"problem": "no aisle between the benches (%.2f m)" % aisle_width, "floor": floor}
        aisle_x, aisle_width = aisle_at, 0.6
    # Saloons whose aisles lie apart across the car (rule 4): an aisle each
    # (the main floor's; a level's stretches are short, its own line).
    saloons = saloon_pieces(benches) if spans is None else None
    if saloons:
        aisle_x, aisle_width = saloons[0][0], saloons[0][1]
    else:
        saloons = [[aisle_x, aisle_width, -math.inf, math.inf]]

    def line(z):
        """The aisle's line (x, width) at z along the car."""
        for x, w, z0_, z1_ in saloons:
            if z <= z1_:
                return x, w
        return saloons[-1][0], saloons[-1][1]

    # The floor along the aisle: its cells (0.25 m) at the floor's height.
    cells = set()
    for t, part, n, a, c in faces:
        if n[1] > 0.95 and all(abs(p[1] - floor) < 0.04 for p in t):
            fx0, fx1 = min(p[0] for p in t), max(p[0] for p in t)
            fz0, fz1 = min(p[2] for p in t), max(p[2] for p in t)
            for x, w, z0_, z1_ in saloons:
                lo_z, hi_z = max(fz0, z0_), min(fz1, z1_)
                if fx0 <= x + 0.15 and fx1 >= x - 0.15 and hi_z > lo_z:
                    for j in range(int(math.floor(lo_z / 0.25)), int(math.ceil(hi_z / 0.25))):
                        cells.add(j)
    if spans is not None:
        # A level's: its stretches (find() makes its aisles of them).
        aisle = [round(aisle_x, 3), round(floor, 3), min(z for z, _ in spans), max(z for _, z in spans)]
    else:
        if not cells:
            return {"problem": "no floor along the aisle", "floor": floor}
        middle = min(cells, key=lambda j: abs(j * 0.25 + 0.125 - sum(b["z0"] + b["z1"] for b in benches) / (2 * len(benches))))
        lo = hi = middle
        while lo - 1 in cells or lo - 2 in cells:
            lo -= 1 if lo - 1 in cells else 2
        while hi + 1 in cells or hi + 2 in cells:
            hi += 1 if hi + 1 in cells else 2
        aisle = [round(aisle_x, 3), round(floor, 3), round(lo * 0.25, 3), round((hi + 1) * 0.25, 3)]

    # Each bench's places. Its backrests: the upright faces above the
    # cushion facing the way its sitters face or away (u: along that way, w:
    # across it, where the places sit side by side).
    seats = []
    for b in benches:
        long_x = (b["x1"] - b["x0"]) >= (b["z1"] - b["z0"]) * 0.8
        top = floor + b["y"]
        ua, wa = (2, 0) if long_x else (0, 2)
        u0, u1 = (b["z0"], b["z1"]) if long_x else (b["x0"], b["x1"])
        w0, w1 = (b["x0"], b["x1"]) if long_x else (b["z0"], b["z1"])
        backs = []
        for t, part, n, a, c in faces:
            if abs(n[1]) > 0.5 or abs(n[ua]) <= 0.6 or not (top + 0.15 < c[1] < top + 0.75):
                continue
            if w0 <= c[wa] <= w1 and u0 - 0.25 < c[ua] < u1 + 0.25:
                backs.append((c[ua], a, c[1] > top + 0.35))
        if sum(a for _, a, _ in backs) < 0.02:
            continue  # no backrest found: not a seat (a shelf, a table)
        # A backrest inside the cushion (above armrests' height: their ends
        # stand inside its front edge) parts a bench across the car: two
        # benches back to back, each facing away from it (their cushions
        # meet under it), or a panel behind a bench (too shallow for
        # places). (back, front) each. Not one along the car: a raised floor
        # at cushion height is one, the seats standing on it its "backrests".
        parts = []
        inner = [(u, a) for u, a, high in backs if long_x and high and u0 + BACK_AT < u < u1 - BACK_AT]
        if sum(a for _, a in inner) >= 0.05:
            ub = sum(u * a for u, a in inner) / sum(a for _, a in inner)
            parts = [(ub, edge) for edge in (u0, u1) if abs(edge - ub) >= MIN_DEPTH]
        if not parts:
            # The back: the side the upright faces over and by the cushion
            # lean to; but not a side without a backrest's height there
            # while the other has one (a counter in front of the last row
            # outweighs its thin backrest, BR3's).
            mid = (u0 + u1) / 2
            to_high = sum(a * (u - mid) for u, a, _ in backs) > 0
            high_hi = sum(a for u, a, high in backs if high and u > mid)
            high_lo = sum(a for u, a, high in backs if high and u < mid)
            if to_high and high_hi < 0.02 <= high_lo:
                to_high = False
            elif not to_high and high_lo < 0.02 <= high_hi:
                to_high = True
            parts = [(u1, u0)] if to_high else [(u0, u1)]
        for back, front in parts:
            if long_x:
                heading = 0.0 if front > back else 180.0
            else:
                heading = 90.0 if front > back else 270.0
            along = (back + front) / 2
            lo_u, hi_u = min(back, front) - 0.02, max(back, front) + 0.02
            pieces = [(min(p[wa] for p in t), max(p[wa] for p in t)) for t, c in b["faces"] if lo_u <= c[ua] <= hi_u]
            for w in places_across(pieces, w0, w1):
                x, z = (w, along) if long_x else (along, w)
                if not (aisle[2] - 0.5 <= z <= aisle[3] + 0.5) or not on_spans(z, 0.5):
                    continue
                if abs(x - line(z)[0]) < OFF_AISLE:
                    continue  # on the aisle's line: a flip seat on a partition, a ledge
                seats.append([round(x, 3), round(top, 3), round(z, 3), heading])
    seats.sort(key=lambda s: (s[2], s[0]))

    # Partitions across the car blocking the aisle: walls (upright, facing
    # along the car) over the aisle between knee and head height.
    walls = collections.defaultdict(float)
    for t, part, n, a, c in faces:
        if abs(n[2]) > 0.9 and abs(c[0] - line(c[2])[0]) < line(c[2])[1] / 2 and floor + 0.3 < c[1] < floor + 1.8:
            walls[round(c[2] / 0.1)] += a
    passages = []
    for key in sorted(walls):
        if walls[key] < 0.25 or not (aisle[2] < key * 0.1 < aisle[3]):
            continue
        if passages and key * 0.1 - passages[-1][2] < 0.25:
            continue
        x, w = line(key * 0.1)
        passages.append([round(x, 3), round(floor, 3), round(key * 0.1, 3), round(min(w, 0.8), 2)])

    # A cab: a room at the car's end behind a partition across the car (most
    # of its width covered both above a seat's backrest and up to head
    # height), END_ROOM deep from the model's end, with a crew's few places
    # (CAB_PLACES at most). Its seats are the crew's.
    bands = ((floor + 1.35, floor + 1.65), (floor + 1.65, floor + 2.0))
    across = collections.defaultdict(lambda: ([], []))
    for t, part, n, a, c in faces:
        if abs(n[2]) > 0.9:
            ys = [p[1] for p in t]
            for k, (y0, y1) in enumerate(bands):
                if min(ys) <= y1 and max(ys) >= y0:
                    across[round(c[2] / 0.1)][k].append((min(p[0] for p in t), max(p[0] for p in t)))
    cut = [-float("inf"), float("inf")]
    for key, spans in across.items():
        if min(covered(s, half) for s in spans) < 0.5:
            continue
        w = key * 0.1
        if END_ROOM[0] <= z1 - w <= END_ROOM[1]:
            cut[1] = min(cut[1], w)
        if END_ROOM[0] <= w - z0 <= END_ROOM[1]:
            cut[0] = max(cut[0], w)
    if sum(1 for s in seats if s[2] <= cut[0]) > CAB_PLACES:
        cut[0] = -float("inf")
    if sum(1 for s in seats if s[2] >= cut[1]) > CAB_PLACES:
        cut[1] = float("inf")
    seats = [s for s in seats if cut[0] < s[2] < cut[1]]
    return {"floor": floor, "aisle": aisle, "aisleWidth": round(aisle_width, 2), "benches": len(benches),
            "seats": seats, "passages": passages, "cut": cut, "saloons": saloons}


# Floor levels (task humans 12, the user's rule 5 of task 11: a low-floor
# middle and raised ends): a floor covering the aisle's line LEVEL_RUN or
# more along the car, LEVEL from the main floor in height (less is the same
# floor; more is a deck above or a pit below).
LEVEL = (0.15, 1.2)
LEVEL_RUN = 1.5
RUN_LEAST = 1.0      # a stretch shorter than this is no aisle (m)
CELL = 0.25


def covers(t, x, z):
    """Whether a triangle, seen from above, covers the point (x, z)."""
    (ax, az), (bx, bz), (cx, cz) = [(p[0], p[2]) for p in t]
    d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
    if abs(d) < 1e-12:
        return False
    l1 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / d
    l2 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / d
    return l1 >= -1e-6 and l2 >= -1e-6 and 1 - l1 - l2 >= -1e-6


def floor_runs(tris, x):
    """The flat upward faces covering the line along the car at x, by
    height (heights within LEVEL_STEP one floor, its area's mean): [(height,
    area, [(z0, z1), ...])], the stretches in CELL steps with gaps of up to
    two cells bridged, those RUN_LEAST long or more."""
    faces = []
    for t, part in tris:
        n, a = normal_area(t)
        if a <= 0 or n[1] <= 0.95:
            continue
        y = sum(p[1] for p in t) / 3
        if not 0.3 < y < 2.2 or max(abs(p[1] - y) for p in t) > 0.03:
            continue
        if min(p[0] for p in t) > x or max(p[0] for p in t) < x:
            continue
        faces.append((t, y, a))
    clusters = []  # [mean height, area, cells]
    for t, y, a in sorted(faces, key=lambda f: -f[2]):
        for c in clusters:
            if abs(c[0] - y) <= LEVEL_STEP:
                c[0] = (c[0] * c[1] + y * a) / (c[1] + a)
                c[1] += a
                break
        else:
            c = [y, a, set()]
            clusters.append(c)
        for j in range(int(math.floor(min(p[2] for p in t) / CELL)), int(math.ceil(max(p[2] for p in t) / CELL))):
            if covers(t, x, (j + 0.5) * CELL):
                c[2].add(j)
    out = []
    for y, a, cells in clusters:
        runs = []
        for j in sorted(cells):
            if runs and j <= runs[-1][1] + 3:
                runs[-1][1] = j
            else:
                runs.append([j, j])
        runs = [(round(lo * CELL, 3), round((hi + 1) * CELL, 3)) for lo, hi in runs if (hi + 1 - lo) * CELL >= RUN_LEAST]
        if runs:
            out.append((round(y, 3), a, runs))
    return out


def floor_runs_on(tris, saloons):
    """floor_runs() along a car's aisle lines: each saloon's within its part
    of the car (rule 4), each floor's stretches joined again."""
    if len(saloons) == 1:
        return floor_runs(tris, round(saloons[0][0], 3))
    clusters = []  # [height, area, stretches]
    for x, w, a0, a1 in saloons:
        for y, area, runs in floor_runs(tris, x):
            runs = [(max(z0, a0), min(z1, a1)) for z0, z1 in runs if min(z1, a1) > max(z0, a0)]
            if not runs:
                continue
            for c in clusters:
                if abs(c[0] - y) <= LEVEL_STEP:
                    c[1] += area
                    c[2] += runs
                    break
            else:
                clusters.append([y, area, runs])
    out = []
    for y, area, runs in clusters:
        joined = []
        for z0, z1 in sorted(runs):
            if joined and z0 <= joined[-1][1] + 3 * CELL:
                joined[-1][1] = max(joined[-1][1], z1)
            else:
                joined.append([z0, z1])
        joined = [(round(z0, 3), round(z1, 3)) for z0, z1 in joined if z1 - z0 >= RUN_LEAST]
        if joined:
            out.append((round(y, 3), area, joined))
    return out


def line_at(saloons, z):
    """The x of a car's aisle line at z along it."""
    for x, w, z0, z1 in saloons:
        if z <= z1:
            return x
    return saloons[-1][0]


# A double-deck car's upper deck (the user's report, 2026-11-18: the Bhp):
# its floor this high above the rail (m), over the usual floor's band (0.6
# to 1.8 m, _find()), at least DECK_RISE above the doors' floor.
DECK = (1.8, 3.2)
DECK_RISE = 1.0
# A deck has this many seats or more (fewer: a loco's roof, an EN57's
# luggage racks).
DECK_SEATS = 8


def stairs_foot(tris, x, low, high, deck_end, outward):
    """Where stairs over the line at x from a floor at `low` up to a deck at
    `high` begin: the treads (upward faces between the two heights covering
    the line) running on from the deck's end (`deck_end`) the way `outward`
    goes (+1 along the car, -1 back), each at most 0.3 m on from the last;
    None without treads there."""
    spans = []
    for t, part in tris:
        n, a = normal_area(t)
        if a <= 0 or n[1] <= 0.85:
            continue
        y = sum(p[1] for p in t) / 3
        if not low + 0.1 < y < high - 0.05:
            continue
        if min(p[0] for p in t) > x or max(p[0] for p in t) < x:
            continue
        zs = [outward * p[2] for p in t]
        spans.append((min(zs), max(zs)))
    reach, found = outward * deck_end, False
    for z0, z1 in sorted(spans):
        if z1 <= reach:
            continue
        if z0 > reach + 0.3:
            break
        reach, found = z1, True
    return outward * reach if found else None


def upper_deck(tris, entrance):
    """A double-deck car's upper deck, where the usual floor (`entrance`, the
    doors' floor between the decks) has no seats: the floor DECK high with
    the most area its main floor, its seats and aisles as _find() finds them;
    and the doors' floor along the deck's aisle line beyond its ends (the
    entrances) an aisle each, up to the foot of its stairs up (TSRE joins it
    to the deck: people step in there and go up the stairs). The lower deck
    (its way in down side stairs, a bench across the aisle's line under the
    stairs up: the Bhp's) is not taken. None without a deck with seats."""
    if entrance is None:
        return None
    xs = [p[0] for t, _ in tris for p in t]
    zs = [p[2] for t, _ in tris for p in t]
    half = max(abs(percentile(xs, 0.02)), abs(percentile(xs, 0.98)))
    z0, z1 = percentile(zs, 0.01), percentile(zs, 0.99)
    floors = collections.Counter()
    for t, part in tris:
        n, a = normal_area(t)
        c = [sum(p[i] for p in t) / 3 for i in range(3)]
        if a > 0 and n[1] > 0.95 and max(DECK[0], entrance + DECK_RISE) < c[1] < DECK[1] \
                and abs(c[0]) < half - 0.12 and z0 + 0.3 < c[2] < z1 - 0.3:
            floors[round(c[1], 2)] += a
    if not floors:
        return None
    floor = max(floors, key=floors.get)
    deck = _find(tris, floor=floor, exclude=[entrance])
    if "problem" in deck or len(deck["seats"]) < DECK_SEATS:
        return None
    deck.pop("cut")
    saloons = deck.pop("saloons")
    a = deck["aisle"]
    aisles = on_saloons(saloons, a[1], [(a[2], a[3])])
    dz0, dz1 = min(q[2] for q in aisles), max(q[3] for q in aisles)
    spans = []
    for y, area, runs in floor_runs_on(tris, saloons):
        if abs(y - entrance) > LEVEL_STEP:
            continue
        for r0, r1 in runs:
            if r1 <= dz0 + 0.5:
                foot = stairs_foot(tris, line_at(saloons, dz0), entrance, floor, dz0, -1)
                r1 = min(r1, foot) if foot is not None else r1
            elif r0 >= dz1 - 0.5:
                foot = stairs_foot(tris, line_at(saloons, dz1), entrance, floor, dz1, 1)
                r0 = max(r0, foot) if foot is not None else r0
            else:
                continue  # under the deck: no entrance
            if r1 - r0 >= RUN_LEAST:
                spans.append((round(r0, 3), round(r1, 3), round(y, 3)))
    entrances = [[round(line_at(saloons, (r0 + r1) / 2), 3), y, r0, r1] for r0, r1, y in spans]
    aisles = sorted(entrances + aisles, key=lambda q: (q[2], q[1]))
    deck.update({"aisles": aisles, "aisle": aisles[0], "deck": round(floor, 3),
                 "levels": [{"floor": round(entrance, 3), "spans": [s[:2] for s in spans], "seats": 0}] if spans else []})
    if len(saloons) > 1:
        deck["saloons"] = len(saloons)
    return deck


# A double-deck car's lower deck (the user's design, 2026-11-18: the Bhp's,
# down side stairs from its entrance, a bench across the aisle's line under
# its stairs up): its floor at least BELOW_DROP below the doors' floor and
# above BELOW_LEAST; benches across the middle line (BENCH_ACROSS of the line
# covered either side) are seats of their own, the aisle found between them.
BELOW_DROP = 0.4
BELOW_LEAST = 0.15
BENCH_ACROSS = 0.2


def deck_below(tris, entrance, others):
    """A double-deck car's lower deck below its doors' floor (`entrance`):
    {"floor", "seats", "passages", "aisles", "spans"}, None without one with
    seats (DECK_SEATS). `others`: the other floors' heights (not this
    one's)."""
    if entrance is None:
        return None
    xs = [p[0] for t, _ in tris for p in t]
    zs = [p[2] for t, _ in tris for p in t]
    half = max(abs(percentile(xs, 0.02)), abs(percentile(xs, 0.98)))
    z0, z1 = percentile(zs, 0.01), percentile(zs, 0.99)
    floors = collections.Counter()
    for t, part in tris:
        n, a = normal_area(t)
        c = [sum(p[i] for p in t) / 3 for i in range(3)]
        if a > 0 and n[1] > 0.95 and BELOW_LEAST < c[1] < entrance - BELOW_DROP \
                and abs(c[0]) < half - 0.12 and z0 + 0.3 < c[2] < z1 - 0.3:
            floors[round(c[1], 2)] += a
    if not floors:
        return None
    floor = max(floors, key=floors.get)
    runs = [r for y, a, r in floor_runs(tris, 0.0) if abs(y - floor) <= LEVEL_STEP]
    if not runs:
        return None
    lo, hi = min(z for r in runs[0] for z in r), max(z for r in runs[0] for z in r)
    # Benches across the middle line on this floor (under the stairs up).
    across = []
    for t, part in tris:
        n, a = normal_area(t)
        if a <= 0.002 or n[1] <= 0.85:
            continue
        y = sum(p[1] for p in t) / 3
        if not CUSHION[0] < y - floor < CUSHION[1]:
            continue
        if min(p[0] for p in t) < -BENCH_ACROSS and max(p[0] for p in t) > BENCH_ACROSS:
            tz = [p[2] for p in t]
            if lo - 0.3 < min(tz) and max(tz) < hi + 0.3:
                across.append((min(tz), max(tz), y, min(p[0] for p in t), max(p[0] for p in t)))
    benches = []
    for b in sorted(across):
        if benches and b[0] <= benches[-1][1] + 0.05:
            q = benches[-1]
            benches[-1] = (q[0], max(q[1], b[1]), max(q[2], b[2]), min(q[3], b[3]), max(q[4], b[4]))
        else:
            benches.append(b)
    middle = (lo + hi) / 2
    a0 = max([b[1] for b in benches if b[1] < middle] + [lo]) + 0.05
    a1 = min([b[0] for b in benches if b[0] > middle] + [hi]) - 0.05
    if a1 - a0 < RUN_LEAST:
        return None
    deck = _find(tris, floor=floor, spans=[(a0, a1)], aisle_at=0.0, exclude=list(others) + [entrance])
    if "problem" in deck:
        return None
    seats = list(deck["seats"])
    for b0, b1, top, w0, w1 in benches:
        # Facing the deck's middle, its back to the car's end.
        z = (b0 + b1) / 2
        heading = 0.0 if z < middle else 180.0
        for x in places_across([], w0, w1):
            seats.append([round(x, 3), round(top, 3), round(z, 3), heading])
    if len(seats) < DECK_SEATS:
        return None
    a = deck["aisle"]
    return {"floor": round(floor, 3), "seats": seats, "passages": deck["passages"],
            "aisles": on_saloons(deck["saloons"], floor, [(a[2], a[3])]), "spans": [(round(lo, 3), round(hi, 3))]}


# Stairs between two floors' aisles (the user's design, 2026-11-18): every
# level change is written as a sloped aisle along its treads (the line
# through their middles, from where it meets the lower floor to where it
# meets the upper one), on a line where a walker is free (no seat, tread or
# ceiling over head height in the way, BODY either side of the line): the
# aisles' own line, else the nearest free one beside (the Bhp's side stairs
# down past its bench under the stairs up). A ramp (no treads, sloped faces)
# the same way along its faces. Pieces on the floors at the stairs' line
# join them to the aisles where it is not theirs.
STAIRS_RISE = 1.5   # the most a flight rises (m)
STAIRS_RUN = 8.0    # the most run per rise between the two aisles' ends looked over (a 1:8 ramp)
# Steps assumed where two floors meet with nothing modelled between them
# (SA103's raised end, a sheer 0.65 m over its low floor): over the lower
# floor at the higher one's edge, this steep (rise over run).
STEPS_SLOPE = 0.75
# A floor less than this below another over it (most of its length within
# HIDDEN_X across) is no floor people stand on (the Elf's and SA103's faces
# under their raised floors, found as levels with a few places).
HEAD_CLEAR = 1.8
HIDDEN_X = 0.5
BODY = 0.2
# Over the floor along the way: free of benches and of stairs overhead (the
# Bhp's stairs up over the middle line of its stairs down); not up to head
# height, which steep stairs' ceilings come within (the Bhp's over its
# stairs up, 1.2 to 1.5 m over the line).
HEADROOM = (0.3, 1.2)
CONNECT_OVERLAP = 0.4


def face_boxes(tris):
    out = []
    for t, part in tris:
        n, a = normal_area(t)
        if a <= 0.0005:
            continue
        xs, ys, zs = [p[0] for p in t], [p[1] for p in t], [p[2] for p in t]
        out.append((min(xs), max(xs), min(ys), max(ys), min(zs), max(zs), n[1], sum(ys) / 3, sum(zs) / 3))
    return out


def stairs_line(boxes, x, low, high, za, zb, rising):
    """The pitch line of stairs (or a ramp) over the line at x between floors
    `low` and `high`, looked for between za and zb along the car, rising the
    way `rising` goes (+1 along the car, -1 back): (slope, z where it meets
    `low`, z where it meets `high`), None without treads or ramp faces
    there."""
    zlo, zhi = min(za, zb) - 0.6, max(za, zb) + 0.6
    treads = []
    for b in boxes:
        if b[0] > x or b[1] < x or b[5] < zlo or b[4] > zhi:
            continue
        if b[6] > 0.95 and low + 0.05 < b[7] < high - 0.05 and b[3] - b[2] < 0.03:
            treads.append(b)
    clusters = []
    for b in sorted(treads, key=lambda b: b[7]):
        if clusters and abs(clusters[-1][0] - b[7]) <= 0.03:
            c = clusters[-1]
            clusters[-1] = (c[0], min(c[1], b[4]), max(c[2], b[5]))
        else:
            clusters.append((b[7], b[4], b[5]))
    points = [((c[1] + c[2]) / 2, c[0]) for c in clusters if c[2] - c[1] < 1.0]
    if not points:
        # A ramp: sloped faces rising between the floors.
        ramp = [b for b in boxes if b[0] <= x <= b[1] and zlo < b[8] < zhi and 0.6 < b[6] <= 0.995
                and low - 0.05 < b[7] < high + 0.05 and b[3] - b[2] > 0.05]
        points = [(b[8], b[7]) for b in ramp]
        if len(points) < 2:
            return None
    if len(points) == 1:
        z, y = points[0]
        c = clusters[0]
        slope = ((high - low) / 2) / max(c[2] - c[1], 0.15)
        slope *= rising
    else:
        mz = sum(p[0] for p in points) / len(points)
        my = sum(p[1] for p in points) / len(points)
        var = sum((p[0] - mz) ** 2 for p in points)
        if var < 1e-6:
            return None
        slope = sum((p[0] - mz) * (p[1] - my) for p in points) / var
        z, y = mz, my
    if abs(slope) < 0.05 or (slope > 0) != (rising > 0):
        return None
    return slope, z + (low - y) / slope, z + (high - y) / slope


def way_free(boxes, seats, x, way):
    """Whether a walker is free along the line at x over `way`: [(z0, z1,
    floor at z0, floor at z1)] pieces (a floor or stairs): no face over its
    head room (but the floor's own, treads within it) nor a seat within BODY
    of the line plus a place's half width."""
    for z0, z1, y0, y1 in way:
        lo, hi = min(z0, z1), max(z0, z1)

        def floor_at(z):
            k = 0.0 if abs(z1 - z0) < 1e-6 else min(max((z - z0) / (z1 - z0), 0.0), 1.0)
            return y0 + (y1 - y0) * k
        for b in boxes:
            if b[1] < x - BODY or b[0] > x + BODY or b[5] < lo or b[4] > hi:
                continue
            if abs(b[6]) < 0.5:
                continue  # walls along the way, partitions' frames
            f = floor_at(min(max(b[8], lo), hi))
            if b[3] > f + HEADROOM[0] and b[2] < f + HEADROOM[1]:
                return False
        for s in seats:
            if abs(s[0] - x) < BODY + PLACE / 2 and lo - 0.15 < s[2] < hi + 0.15:
                f = floor_at(min(max(s[2], lo), hi))
                if f - 0.1 < s[1] - CUSHION[0] and s[1] < f + 1.0:
                    return False
    return True


# Two floors' stretches overlap this much along the car at most and still
# face each other (their runs reach under the stairs between: SA136's 0.75
# m).
FACING_OVERLAP = 1.0


def facing(low, high):
    """Where two aisles face each other along the car, one before the other
    (the lower one's end, the upper one's end; they may overlap by up to
    FACING_OVERLAP), None where they lie side by side."""
    if (low[2] + low[3]) / 2 <= (high[2] + high[3]) / 2:
        if low[3] <= high[2] + FACING_OVERLAP and low[2] < high[2] and low[3] < high[3]:
            return low[3], high[2]
    elif high[3] <= low[2] + FACING_OVERLAP and high[2] < low[2] and high[3] < low[3]:
        return low[2], high[3]
    return None


def connect_levels(tris, found, log=None):
    """Stairs written between found aisles at a level change (ONE_STEP to
    STAIRS_RISE apart in height, one ending where the other begins along the
    car, STAIRS_RUN times the rise apart at most): a sloped aisle each, on a
    free line, the aisles' ends moved to its ends (or pieces at its line
    joining them). Where none is found, none: they do not join."""
    aisles = [list(a) for a in found.get("aisles", []) if not sloped(a)]
    if len(aisles) < 2:
        return found
    # Floors hidden under others (no head room): out, with their places.
    hidden = []
    for a in aisles:
        for b in aisles:
            if b is a or not ONE_STEP < b[1] - a[1] < HEAD_CLEAR or abs(b[0] - a[0]) >= HIDDEN_X:
                continue
            under = min(a[3], b[3]) - max(a[2], b[2])
            if under >= 0.8 * (a[3] - a[2]):
                hidden.append(a)
                break
    if hidden:
        kept = [a for a in aisles if a not in hidden]
        seats = []
        for q in found.get("seats", []):
            floor = q[1] - CUSHION_USUAL
            holding = [a for a in aisles if a[2] - 0.5 <= q[2] <= a[3] + 0.5] or aisles
            nearest = min(holding, key=lambda a: (abs(a[1] - floor), abs(a[0] - q[0])))
            if nearest not in hidden:
                seats.append(q)
        if log is not None:
            log.append("hidden floors dropped: %s, %d places with them"
                       % (", ".join("%.2f %.2f..%.2f" % (a[1], a[2], a[3]) for a in hidden),
                          len(found.get("seats", [])) - len(seats)))
        found["seats"] = seats
        aisles = kept
        found["aisles"] = aisles
        found["aisle"] = aisles[0]
        if len(aisles) < 2:
            return found
    boxes = face_boxes(tris)
    xs = [p[0] for t, _ in tris for p in t]
    half = max(abs(percentile(xs, 0.02)), abs(percentile(xs, 0.98)))
    seats = found.get("seats", [])
    pairs = []
    for i in range(len(aisles)):
        for j in range(len(aisles)):
            low, high = aisles[i], aisles[j]
            rise = high[1] - low[1]
            if not ONE_STEP < rise <= STAIRS_RISE:
                continue
            ends = facing(low, high)
            if ends is None:
                if log is not None:
                    log.append("A%d %.2f and A%d %.2f side by side: no stairs" % (i + 1, low[1], j + 1, high[1]))
                continue
            if abs(ends[1] - ends[0]) > JOIN_GAP + STAIRS_RUN * rise:
                continue
            pairs.append((abs(ends[1] - ends[0]), i, j))
    stairs, pieces = [], []
    for gap, i, j in sorted(pairs):
        low, high = aisles[i], aisles[j]
        # Their ends as they are now (stairs found before moved some).
        za, zb = facing(low, high)
        rising = 1.0 if (high[2] + high[3]) / 2 > (low[2] + low[3]) / 2 else -1.0
        candidates = [low[0], high[0]]
        for k in range(1, 12):
            for sign in (1, -1):
                candidates.append(round(low[0] + sign * 0.1 * k, 3))
        done = False
        for x in candidates:
            if abs(x) > half - 0.35:
                continue
            line = stairs_line(boxes, x, low[1], high[1], za, zb, rising)
            assumed = False
            if line is None:
                if abs(zb - za) > JOIN_GAP or x not in (low[0], high[0]):
                    continue
                # Nothing modelled between floors that meet: steps assumed
                # over the lower floor at the higher one's edge.
                run = (high[1] - low[1]) / STEPS_SLOPE
                line = (rising * STEPS_SLOPE, zb - rising * run, zb)
                assumed = True
            slope, z_low, z_high = line
            # The way: from the lower aisle's end on to the stairs where
            # they begin beyond it, up them, on to the upper aisle's end
            # where they end short of it (pieces where x is not the aisle's).
            up = 1.0 if z_high > z_low else -1.0
            way = [(z_low, z_high, low[1], high[1])]
            if (z_low - za) * up > 0:
                way.append((za, z_low, low[1], low[1]))
            if (zb - z_high) * up > 0:
                way.append((z_high, zb, high[1], high[1]))
            if not way_free(boxes, seats, x, way):
                continue
            stairs.append(stairs_aisle(x, low[1], z_low, high[1], z_high))
            for a, z_end, end in ((low, z_low, za), (high, z_high, zb)):
                last = abs(end - a[3]) <= abs(end - a[2])
                if abs(a[0] - x) < 0.05:
                    # Its own line: its end moved to the stairs' end.
                    if last:
                        a[3] = round(z_end, 3)
                    else:
                        a[2] = round(z_end, 3)
                else:
                    # A piece at the stairs' line from their end back into
                    # the aisle's range.
                    back = (a[3] - CONNECT_OVERLAP) if last else (a[2] + CONNECT_OVERLAP)
                    pieces.append([round(x, 3), a[1], round(min(z_end, back), 3), round(max(z_end, back), 3)])
            if log is not None:
                log.append("A%d %.2f - A%d %.2f: %s at x %.2f, %.2f..%.2f, slope %.2f"
                           % (i + 1, low[1], j + 1, high[1], "steps assumed" if assumed else "stairs", x,
                              min(z_low, z_high), max(z_low, z_high), slope))
            done = True
            break
        if not done and log is not None:
            log.append("A%d %.2f - A%d %.2f: no free stairs" % (i + 1, low[1], j + 1, high[1]))
    if not stairs:
        found["aisles"] = aisles
        return found
    out = sorted(aisles + pieces + stairs, key=lambda q: (q[2], q[1]))
    found["aisles"] = out
    found["aisle"] = next(a for a in out if not sloped(a))
    return found


def find(tris, log=None):
    """The car's floor, aisles, passages and seats (its model's space), as
    find_main() finds them; a double-deck car's upper deck instead
    (upper_deck()) where it has more seats (the main floor its doors' floor
    between the decks), with its lower deck (deck_below(): the user's
    design, 2026-11-18). Every level change then written as stairs
    (connect_levels())."""
    main = find_main(tris)
    deck = upper_deck(tris, main.get("floor"))
    if deck is None or len(deck["seats"]) <= len(main.get("seats", [])):
        return connect_levels(tris, main, log)
    # Its lower deck too (the user's design, 2026-11-18), down its stairs.
    lower = deck_below(tris, main.get("floor"), [deck["deck"]])
    if lower is not None:
        deck["seats"] = sorted(deck["seats"] + lower["seats"], key=lambda s: (s[2], s[0]))
        deck["passages"] = sorted(deck["passages"] + lower["passages"], key=lambda p: p[2])
        deck["aisles"] = sorted(deck["aisles"] + lower["aisles"], key=lambda q: (q[2], q[1]))
        deck["lower"] = lower["floor"]
        deck.setdefault("levels", []).append({"floor": lower["floor"], "spans": lower["spans"],
                                              "seats": len(lower["seats"])})
    return connect_levels(tris, deck, log)


def find_main(tris):
    """The car's floor, aisles, passages and seats (its model's space): its
    main floor's (the most area) as _find() finds them; where other floor
    levels cover the aisle's line (a raised end, a low-floor middle, a low
    entrance), each level's seats too, and an aisle for each stretch of each
    level (task humans 12: aisles joined where they meet, TSRE finds the
    joins). A car of one floor: its one aisle as before."""
    base = _find(tris)
    if "problem" in base:
        base.pop("cut", None)
        return base
    cut = base.pop("cut")
    saloons = base.pop("saloons")
    a = base["aisle"]
    base["aisles"] = on_saloons(saloons, a[1], [(a[2], a[3])])
    if len(saloons) > 1:
        base["aisle"] = base["aisles"][0]
        base["saloons"] = len(saloons)
    runs = floor_runs_on(tris, saloons)
    main = min(runs, key=lambda r: abs(r[0] - base["floor"]), default=None)
    if main is None or abs(main[0] - base["floor"]) > LEVEL_STEP:
        return base
    levels = [r for r in runs if r is not main and LEVEL[0] <= abs(r[0] - base["floor"]) <= LEVEL[1]
              and sum(z1 - z0 for z0, z1 in r[2]) >= LEVEL_RUN]
    if not levels:
        return base
    heights = [main[0]] + [r[0] for r in levels]

    def trimmed(spans):
        # Not into a cab (the room behind a partition at the car's end).
        out = [(max(z0, cut[0]), min(z1, cut[1])) for z0, z1 in spans]
        return [(z0, z1) for z0, z1 in out if z1 - z0 >= RUN_LEAST]
    aisles = on_saloons(saloons, main[0], trimmed(main[2]))
    seats = list(base["seats"])
    passages = list(base["passages"])
    # The main floor's stretches beyond the one its aisle was found on (a
    # saloon past a low middle, SA103's): their seats too.
    a = base["aisle"]
    beyond = [(z0, z1) for z0, z1 in trimmed(main[2]) if z1 < a[2] - 0.5 or z0 > a[3] + 0.5]
    if beyond:
        middle = sum(z0 + z1 for z0, z1 in beyond) / (2 * len(beyond))
        more = _find(tris, floor=base["floor"], spans=beyond, aisle_at=line_at(saloons, middle),
                     exclude=[r[0] for r in levels])
        if "problem" not in more:
            seats += more["seats"]
            passages += [p for p in more["passages"] if any(z0 <= p[2] <= z1 for z0, z1 in beyond)]
    found_levels = []
    for y, area, spans in levels:
        spans = trimmed(spans)
        if not spans:
            continue
        middle = sum(z0 + z1 for z0, z1 in spans) / (2 * len(spans))
        level = _find(tris, floor=y, spans=spans, aisle_at=line_at(saloons, middle),
                      exclude=[h for h in heights if h != y])
        if "problem" not in level:
            # A cushion between two floors' heights found from both: the
            # first floor's (the main one's) seat.
            level["seats"] = [q for q in level["seats"] if not any(
                abs(q[0] - s[0]) < 0.12 and abs(q[2] - s[2]) < 0.12 and abs(q[1] - s[1]) < 0.05 and q[3] == s[3]
                for s in seats)]
        if "problem" in level or not level["seats"]:
            # No seats there (an entrance's floor a step below the saloon):
            # no aisle either; people step in onto the main floor's, as on a
            # car of one floor.
            continue
        # Its own aisle's line where its benches stand either side (each
        # saloon's, rule 4).
        seats += level["seats"]
        passages += [p for p in level["passages"] if any(z0 <= p[2] <= z1 for z0, z1 in spans)]
        aisles += on_saloons(level["saloons"], y, spans)
        found_levels.append({"floor": round(y, 3), "spans": spans, "seats": len(level["seats"])})
    if not found_levels and len(seats) == len(base["seats"]):
        return base
    if not found_levels:
        # No level with seats: the main floor's aisles alone.
        aisles = [q for q in aisles if abs(q[1] - main[0]) <= LEVEL_STEP]
    # Seats in order along the car, as one level's are.
    seats.sort(key=lambda s: (s[2], s[0]))
    aisles.sort(key=lambda a: (a[2], a[1]))
    base.update({"aisles": aisles, "aisle": aisles[0], "seats": seats, "passages": sorted(passages, key=lambda p: p[2]),
                 "levels": found_levels})
    return base


def places_across(pieces, w0, w1):
    """Where a bench's places sit across it: its cushion's pieces (faces
    joined where they touch) up to PIECE wide one place each (armchairs,
    single seats), wider ones a place every PLACE; pieces narrower than
    PIECE's least left out while others are seat-sized; else as one bench
    from w0 to w1."""
    joined = []
    for lo, hi in sorted(pieces):
        if joined and lo <= joined[-1][1] + 0.03:
            joined[-1][1] = max(joined[-1][1], hi)
        else:
            joined.append([lo, hi])
    out = []
    for lo, hi in joined:
        width = hi - lo
        if width < PIECE[0]:
            continue
        k = 1 if width <= PIECE[1] else max(1, int(round(width / PLACE)))
        out += [lo + width * (i + 0.5) / k for i in range(k)]
    if not out:
        k = max(1, int(round((w1 - w0) / PLACE)))
        out = [w0 + (w1 - w0) * (i + 0.5) / k for i in range(k)]
    return out


def covered(spans, half):
    """The share of the car's width (-half..half) the spans cover."""
    total, reach = 0.0, -half
    for lo, hi in sorted(spans):
        lo, hi = max(lo, -half), min(hi, half)
        if hi > reach:
            total += hi - max(lo, reach)
            reach = hi
    return total / (2 * half) if half > 0 else 0.0


def split_doors(tris, gap=0.35):
    """One side's door triangles as doors (TSRE's CarDoors::split): joined
    along the car while their spans leave at most `gap` between them (a
    leaf's corners lie apart, so spans, not points); 0.4 to 4.5 m wide."""
    spans = sorted(((min(p[2] for p in t), max(p[2] for p in t), t) for t in tris), key=lambda s: s[0])
    out, i = [], 0
    while i < len(spans):
        reach, j = spans[i][1], i + 1
        while j < len(spans) and spans[j][0] <= reach + gap:
            reach = max(reach, spans[j][1])
            j += 1
        z0 = spans[i][0]
        points = [p for _, _, t in spans[i:j] for p in t]
        if 0.4 <= reach - z0 <= 4.5:
            out.append([round(sum(p[0] for p in points) / len(points), 3), round(min(p[1] for p in points), 3),
                        round((z0 + reach) / 2, 3), round(reach - z0, 3)])
        i = j
    return out


def find_doors(tris):
    """A car's doors from its door parts (an S file's DOOR_A..DOOR_F and the
    parts under them): [x, y, z, width] each, its left side's first (-x as
    written), along the car. Each triangle goes to the side it is on, not
    the side its part's name gives (a part may hold both sides' leaves,
    EN57-787's); triangles nearer the car's middle than DOOR_OFF_MIDDLE are
    not a door's (a saloon's sliding doors, a roof's hatch under a DOOR_
    name: EN57-787's joined its door into one piece too wide), left out,
    and doors whose outer face is nearer it than DOOR_SIDE (a saloon's
    sliding doors)."""
    sides = ([], [])
    for t, name in tris:
        x = sum(p[0] for p in t) / 3
        if DOOR_PART.match(name.split("/")[0]) and abs(x) >= DOOR_OFF_MIDDLE:
            sides[0 if x < 0 else 1].append(t)
    return [d for side in sides for d in sorted(split_doors(side), key=lambda d: d[2]) if abs(d[0]) >= DOOR_SIDE]


# --- Checking ----------------------------------------------------------------
# Aisles join as TSRE joins them at load (ShapeSeats::joins, task humans 12):
# where their ranges overlap or leave at most JOIN_GAP between them, their
# floors there at most ONE_STEP apart (a step); a level change of more is a
# sloped aisle of its own (stairs, a ramp: [x, y, z0, z1, number, y1], the
# floor y at z0 and y1 at z1), joined at its ends only (the user's design,
# 2026-11-18: aisles written apart are meant apart).
JOIN_GAP, LEVEL_STEP, ONE_STEP = 0.3, 0.08, 0.25


def sloped(a):
    """Whether an aisle is stairs or a ramp (its floor rising along it)."""
    return len(a) > 5 and abs(a[5] - a[1]) > LEVEL_STEP


def y_at(a, z):
    """An aisle's floor at z along it (beyond its ends as at them)."""
    if not sloped(a) or a[3] <= a[2]:
        return a[1]
    k = min(max((z - a[2]) / (a[3] - a[2]), 0.0), 1.0)
    return a[1] + (a[5] - a[1]) * k


def stairs_aisle(x, y0, z0, y1, z1):
    """A sloped aisle from (z0, floor y0) to (z1, floor y1), its ends in
    order along the car."""
    if z0 > z1:
        z0, z1, y0, y1 = z1, z0, y1, y0
    return [round(x, 3), round(y0, 3), round(z0, 3), round(z1, 3), 0, round(y1, 3)]


def aisle_joins(aisles):
    """[(a, b, za, zb)]: aisle indices, where the way leaves a and reaches b."""
    def end_onto(s, z, o):
        if z < o[2] - JOIN_GAP or z > o[3] + JOIN_GAP:
            return None
        zo = min(max(z, o[2]), o[3])
        if sloped(o):
            if abs(zo - o[2]) <= JOIN_GAP:
                zo = o[2]
            elif abs(zo - o[3]) <= JOIN_GAP:
                zo = o[3]
            else:
                return None
        return zo if abs(y_at(o, zo) - y_at(s, z)) <= ONE_STEP else None
    out = []
    for i in range(len(aisles)):
        for j in range(i + 1, len(aisles)):
            p, q = aisles[i], aisles[j]
            if not sloped(p) and not sloped(q):
                if abs(p[1] - q[1]) > ONE_STEP:
                    continue
                overlap = min(p[3], q[3]) - max(p[2], q[2])
                if overlap < -JOIN_GAP:
                    continue
                if overlap >= 0:
                    z = (max(p[2], q[2]) + min(p[3], q[3])) / 2
                    out.append((i, j, z, z))
                elif p[3] <= q[2]:
                    out.append((i, j, p[3], q[2]))
                else:
                    out.append((i, j, p[2], q[3]))
                continue
            first = len(out)
            for a, b in ((i, j), (j, i)):
                st, o = aisles[a], aisles[b]
                if not sloped(st):
                    continue
                for z in (st[2], st[3]):
                    at = end_onto(st, z, o)
                    if at is None:
                        continue
                    if not any((n[0], n[1], n[2], n[3]) in ((b, a, at, z), (a, b, z, at)) for n in out[first:]):
                        out.append((a, b, z, at))
    return out


def plan(tris, found, out_svg, title):
    """A top view (SVG): along the car left to right (z), across up (x);
    cushions grey, backrests dark, the aisles cyan with their numbers and
    floor heights (A1 1.07) and their joins orange, passages yellow, seats
    green with a tick the way the sitter faces and their numbers (a seat's
    5th value, else its place in the list from 1: the numbers write gives),
    doors magenta on the car's side with their numbers (D1, D2...)."""
    scale = 60.0
    zs = [p[2] for t, _ in tris for p in t] or [-1.0, 1.0]
    z0, z1 = percentile(zs, 0.01) - 0.5, percentile(zs, 0.99) + 0.5
    width, height = (z1 - z0) * scale, 4.0 * scale

    def X(z):
        return (z - z0) * scale

    def Y(x):
        return height / 2 - x * scale
    floors = [found.get("floor", 0.0)] + [level["floor"] for level in found.get("levels", [])]
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d">' % (width, height + 20),
             '<rect width="100%%" height="100%%" fill="white"/><text x="4" y="%d" font-size="13">%s</text>'
             % (height + 15, title.replace("&", "&amp;").replace("<", "&lt;"))]
    for t, _ in tris:
        n, a = normal_area(t)
        c = [sum(p[i] for p in t) / 3 for i in range(3)]
        if n[1] > 0.85 and any(CUSHION[0] < c[1] - floor < CUSHION[1] for floor in floors):
            parts.append('<polygon points="%s" fill="#bbb"/>' % " ".join("%.1f,%.1f" % (X(p[2]), Y(p[0])) for p in t))
        elif abs(n[1]) < 0.3 and any(floor + 0.6 < c[1] < floor + 1.2 for floor in floors) and a > 0.001:
            parts.append('<polygon points="%s" fill="none" stroke="#555" stroke-width="0.5"/>'
                         % " ".join("%.1f,%.1f" % (X(p[2]), Y(p[0])) for p in t))
    aisles = found.get("aisles") or ([found["aisle"]] if "aisle" in found else [])
    for i, a in enumerate(aisles):
        number = int(a[4]) if len(a) > 4 and a[4] else i + 1
        if sloped(a):
            # Stairs or a ramp: purple, both ends' floors.
            parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#a3c" stroke-width="3"'
                         ' stroke-dasharray="4,2"/>' % (X(a[2]), Y(a[0]), X(a[3]), Y(a[0])))
            parts.append('<text x="%.1f" y="%.1f" font-size="10" fill="#729">A%d %.2f-%.2f</text>'
                         % (X(a[2]) + 2, Y(a[0]) + 11, number, a[1], a[5]))
            continue
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#0bd" stroke-width="2"/>'
                     % (X(a[2]), Y(a[0]), X(a[3]), Y(a[0])))
        parts.append('<text x="%.1f" y="%.1f" font-size="10" fill="#08a">A%d %.2f</text>'
                     % (X(a[2]) + 2, Y(a[0]) - 3, number, a[1]))
    for i, j, za, zb in aisle_joins(aisles):
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#f70" stroke-width="3"/>'
                     % (X(za), Y(aisles[i][0]), X(zb), Y(aisles[j][0])))
    for p in found.get("passages", []):
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#eb0" stroke-width="3"/>'
                     % (X(p[2]), Y(p[0] - p[3] / 2), X(p[2]), Y(p[0] + p[3] / 2)))
    for i, s in enumerate(found.get("seats", [])):
        h = math.radians(s[3])
        number = int(s[4]) if len(s) > 4 else i + 1
        parts.append('<circle cx="%.1f" cy="%.1f" r="4" fill="#1a1"/>' % (X(s[2]), Y(s[0])))
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#1a1" stroke-width="2"/>'
                     % (X(s[2]), Y(s[0]), X(s[2] + 0.3 * math.cos(h)), Y(s[0] + 0.3 * math.sin(h))))
        # The number behind the seat (away from its tick).
        parts.append('<text x="%.1f" y="%.1f" font-size="10" fill="#c00" text-anchor="middle">%d</text>'
                     % (X(s[2] - 0.2 * math.cos(h)), Y(s[0] - 0.2 * math.sin(h)) + 3.5, number))
    for i, d in enumerate(found.get("doors", [])):
        number = int(d[4]) if len(d) > 4 else i + 1
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#d0d" stroke-width="5"/>'
                     % (X(d[2] - d[3] / 2), Y(d[0]), X(d[2] + d[3] / 2), Y(d[0])))
        out_y = Y(d[0]) + (-10 if d[0] > 0 else 16)
        parts.append('<text x="%.1f" y="%.1f" font-size="11" fill="#909" text-anchor="middle">D%d</text>'
                     % (X(d[2]), out_y, number))
    parts.append("</svg>")
    open(out_svg, "w").write("\n".join(parts))


def plan_name(model, base=None):
    """A plan's file name: the model's path under base, '/' as '__'."""
    rel = os.path.relpath(model, base) if base else os.path.basename(model)
    return os.path.splitext(rel.replace(os.sep, "__"))[0] + ".svg"


# --- Writing -----------------------------------------------------------------
def block(found):
    lines = ["\tESD_TSRESeats ("]
    if found.get("seats"):
        lines += aisle_lines(found.get("aisles") or [found["aisle"]])
        for p in found["passages"]:
            lines.append("\t\tPassage ( %s )" % " ".join(fmt(v) for v in p))
        for i, s in enumerate(found["seats"]):
            lines.append("\t\tSeat %d ( %s )" % (i + 1, " ".join(fmt(v) for v in s[:4])))
    lines += door_lines(found.get("doors", []))
    lines.append("\t)")
    return lines


def door_lines(doors):
    return ["\t\tDoor %d ( %s )" % (i + 1, " ".join(fmt(v) for v in d[:4])) for i, d in enumerate(doors)]


def aisle_lines(aisles):
    """Aisles numbered from 1 (`Aisle 2 ( x y z0 z1 )`, task humans 12), as
    on the plans (A1, A2...); stairs with the floor at z1 fifth."""
    return ["\t\tAisle %d ( %s )" % (i + 1, " ".join(fmt(v) for v in a[:4] + ([a[5]] if sloped(a) else [])))
            for i, a in enumerate(aisles)]


def fmt(v):
    text = ("%.3f" % v).rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def read_sd(path):
    raw = open(path, "rb").read()
    if raw[:2] == b"\xff\xfe":
        return raw[2:].decode("utf-16-le")
    return raw.decode("utf-8", "replace")


def block_span(text, name):
    """(start, end) of the `name ( ... )` block's line(s), or None."""
    m = re.search(r"^[ \t]*" + name + r"\s*\(", text, re.M | re.I)
    if not m:
        return None
    depth, i = 0, m.end() - 1
    while i < len(text):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    end = text.find("\n", i)
    return m.start(), end + 1 if end >= 0 else len(text)


def remove_block(text, name):
    """The text without its `name ( ... )` block (balanced brackets)."""
    span = block_span(text, name)
    return text if span is None else text[:span[0]] + text[span[1]:]


def sd_path(model):
    return os.path.splitext(model)[0] + ".sd"


# --stage: {"root": the game root, "dir": the staging folder}.
STAGE = {}


def staged(path):
    """Where a content file is written: with --stage under the staging
    folder by its path under --root, else in place."""
    if not STAGE:
        return path
    rel = os.path.relpath(os.path.abspath(path), STAGE["root"])
    if rel.startswith(".."):
        sys.exit("%s is not under --root %s" % (path, STAGE["root"]))
    return os.path.join(STAGE["dir"], rel)


def sd_read(model):
    """A model's .sd as it stands: the staged one if there is one."""
    sd = sd_path(model)
    return staged(sd) if os.path.exists(staged(sd)) else sd


def sd_seats(model):
    """A model's .sd block as written: {"aisles", "aisle", "passages",
    "seats" ([x, y, z, heading, number]), "doors" ([x, y, z, width,
    number]), "floor"}; None without one."""
    sd = sd_read(model)
    if not os.path.exists(sd):
        return None
    text = read_sd(sd)
    span = block_span(text, "ESD_TSRESeats")
    if span is None:
        return None
    inner = text[span[0]:span[1]]
    inner = inner[inner.index("(") + 1:]
    out = {"aisles": [], "passages": [], "seats": [], "doors": []}
    for name, label, body in re.findall(r"(\w+)\s*([^\s()]*)\s*\(([^()]*)\)", inner):
        try:
            values = [float(v) for v in body.split()]
        except ValueError:
            continue
        if name.lower() == "seat" and len(values) >= 4:
            number = int(label) if label.isdigit() else len(out["seats"]) + 1
            out["seats"].append(values[:4] + [number])
        elif name.lower() == "aisle" and len(values) >= 4:
            number = int(label) if label.isdigit() else len(out["aisles"]) + 1
            x, y0, z0, z1 = values[:4]
            y1 = values[4] if len(values) >= 5 else y0
            if z0 > z1:
                z0, z1, y0, y1 = z1, z0, y1, y0
            out["aisles"].append([x, y0, z0, z1, number] + ([y1] if abs(y1 - y0) > LEVEL_STEP else []))
        elif name.lower() == "passage" and len(values) >= 4:
            out["passages"].append(values[:4])
        elif name.lower() == "door" and len(values) >= 4:
            number = int(label) if label.isdigit() else len(out["doors"]) + 1
            out["doors"].append(values[:4] + [number])
    if out["aisles"]:
        out["aisle"] = out["aisles"][0]
        out["floor"] = out["aisles"][0][1]
    return out


def sd_text(model_path, found):
    sd = sd_path(model_path)
    new = block(found)
    if os.path.exists(sd_read(model_path)):
        text = read_sd(sd_read(model_path)).replace("\r\n", "\n")
        text = remove_block(text, "ESD_TSRESeats")
        close = text.rstrip().rfind(")")
        text = text[:close].rstrip("\n") + "\n" + "\n".join(new) + "\n" + text[close:]
    elif model_path.lower().endswith(".e3d"):
        # Beside a MaSzyna model only the block: its .sd's other lines mean
        # more there (ESD_Alternative_Texture: a season folder).
        text = "\n".join([HEADER, "", "Shape ( %s" % os.path.basename(model_path)] + new + [")", ""])
    else:
        text = "\n".join([HEADER, "", "Shape ( %s" % os.path.basename(model_path), "\tESD_Detail_Level ( 0 )",
                          "\tESD_Alternative_Texture ( 0 )"] + new + [")", ""])
    return sd, text


def write_sd(sd, text):
    """The .sd (its content path) written where it goes (staged() of it)."""
    sd = staged(sd)
    os.makedirs(os.path.dirname(sd) or ".", exist_ok=True)
    with open(sd, "wb") as f:
        f.write(b"\xff\xfe" + text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-16-le"))


def archived(archive, root, files, readme=None, backup=False):
    """<archive>.zip (or -backup.zip) of the files that exist, by their
    paths under root: the backup the content's .sd files, the other the
    written ones (with --stage, the staged copies, by the same paths)."""
    root = os.path.abspath(root)
    if not backup and STAGE:
        files = [staged(f) for f in files]
        root = STAGE["dir"]
    name = archive + ("-backup.zip" if backup else ".zip")
    os.makedirs(os.path.dirname(os.path.abspath(name)), exist_ok=True)
    with zipfile.ZipFile(name, "w", zipfile.ZIP_DEFLATED) as z:
        if readme:
            z.write(readme, "README.md")
        for f in files:
            if os.path.exists(f):
                z.write(f, os.path.relpath(os.path.abspath(f), root))
    return name


def archive_stage(root, archive, readme=None):
    """The staging folder as a set to apply (after write, doors and drop,
    whose own archives each hold their step only): <archive>.zip with every
    staged .sd by its path under root, NEW-SD.txt (those the content has no
    .sd for: to delete on undo) and the readme; <archive>-backup.zip the
    content's .sd at the same paths, as they are."""
    staged_files = [os.path.join(d, f) for d, _, fs in os.walk(STAGE["dir"]) for f in fs if f.lower().endswith(".sd")]
    content = [os.path.join(STAGE["root"], os.path.relpath(f, STAGE["dir"])) for f in sorted(staged_files)]
    new = [os.path.relpath(c, STAGE["root"]) for c in content if not os.path.exists(c)]
    archived(archive, root, content, backup=True)
    archived(archive, root, content, readme)
    with zipfile.ZipFile(archive + ".zip", "a") as z:
        z.writestr("NEW-SD.txt", "".join(n.replace(os.sep, "/") + "\n" for n in new))
    print("%s.zip: %d .sd (%d new), %s-backup.zip: %d" % (archive, len(content), len(new), archive,
                                                         len(content) - len(new)))


def write(models, root, archive, tsre, min_seats, readme=None, plans_dir=None):
    plans, skipped = [], []
    for model in models:
        if CAB.search(os.path.basename(model)):
            skipped.append((model, "a cab's model"))
            continue
        try:
            tris = triangles(model, tsre)
            found = find(tris)
            # MaSzyna's doors are not written: TSRE reads them by the .mmd's
            # door prefix (a part named door_b... is no DOOR_B).
            doors = find_doors(tris) if model.lower().endswith(".s") else []
        except Exception as e:  # a model the readers cannot take
            found, doors = {"problem": repr(e)[:120]}, []
        if "problem" in found or len(found["seats"]) < min_seats:
            if not doors:
                skipped.append((model, found.get("problem", "%d seats" % len(found.get("seats", [])))))
                continue
            # Its doors only.
            found = {"seats": [], "passages": [], "floor": found.get("floor", 0.0)}
        found["doors"] = doors
        plans.append((model,) + sd_text(model, found) + (found, tris))
    finish_writing(plans, root, archive, readme, plans_dir)
    for model, why in skipped:
        print("SKIPPED %s: %s" % (os.path.relpath(os.path.abspath(model), os.path.abspath(root)), why))
    print("%d written, %d skipped; %s.zip, %s-backup.zip" % (len(plans), len(skipped), archive, archive))


def finish_writing(plans, root, archive, readme, plans_dir):
    """The .sd files written (or staged) between their archives, and their
    plans drawn from them."""
    relative = lambda p: os.path.relpath(os.path.abspath(p), os.path.abspath(root))  # noqa: E731
    archived(archive, root, [sd for _, sd, _, _, _ in plans], backup=True)
    for model, sd, text, found, tris in plans:
        write_sd(sd, text)
    archived(archive, root, [sd for _, sd, _, _, _ in plans], readme)
    if plans_dir:
        os.makedirs(plans_dir, exist_ok=True)
    for model, sd, text, found, tris in plans:
        written = sd_seats(model) or {}
        print("WROTE %s%s: %d seats, %d passages, %d doors" % (
            relative(sd), " (staged)" if STAGE else "", len(written.get("seats", [])),
            len(written.get("passages", [])), len(written.get("doors", []))))
        if plans_dir:
            plan(tris, written, os.path.join(plans_dir, plan_name(model, root)),
                 "%s: %d seats, %d doors (its .sd)" % (relative(model), len(written.get("seats", [])),
                                                       len(written.get("doors", []))))


def doors(models, root, archive, tsre, readme=None, plans_dir=None):
    """Only the Door lines of models' .sd: those found in their door parts
    replace the block's Door lines, its other lines kept; a block (or an .sd)
    made where there is none. Models without door parts are left as they
    are."""
    plans, skipped = [], []
    for model in models:
        if CAB.search(os.path.basename(model)):
            skipped.append((model, "a cab's model"))
            continue
        if not model.lower().endswith(".s"):
            skipped.append((model, "not an S file (MaSzyna's doors come from its .mmd)"))
            continue
        try:
            tris = triangles(model, tsre)
            found = find_doors(tris)
        except Exception as e:  # a model the readers cannot take
            skipped.append((model, repr(e)[:120]))
            continue
        if not found:
            skipped.append((model, "no door parts"))
            continue
        sd = sd_path(model)
        text = read_sd(sd_read(model)).replace("\r\n", "\n") if os.path.exists(sd_read(model)) else None
        span = block_span(text, "ESD_TSRESeats") if text is not None else None
        if span is None:
            sd, text = sd_text(model, {"seats": [], "passages": [], "doors": found})
        else:
            inner = re.sub(r"^[ \t]*Door\b[^()\n]*\([^()]*\)[ \t]*\n", "", text[span[0]:span[1]], flags=re.M | re.I)
            close = inner.rstrip().rfind(")")
            line = inner.rfind("\n", 0, close) + 1
            inner = inner[:line] + "\n".join(door_lines(found)) + "\n" + inner[line:]
            text = text[:span[0]] + inner + text[span[1]:]
        plans.append((model, sd, text, {"doors": found}, tris))
    finish_writing(plans, root, archive, readme, plans_dir)
    for model, why in skipped:
        print("SKIPPED %s: %s" % (os.path.relpath(os.path.abspath(model), os.path.abspath(root)), why))
    print("%d written, %d skipped; %s.zip, %s-backup.zip" % (len(plans), len(skipped), archive, archive))


def drop(model, numbers, root, archive):
    """Seats (12) and doors (d2) taken out of a model's .sd by their numbers
    (the plans'); the others keep theirs. The content's .sd in
    <archive>-backup.zip, the written (or staged) one in <archive>.zip."""
    sd = sd_path(model)
    source = sd_read(model)
    raw = open(source, "rb").read()
    utf16 = raw[:2] == b"\xff\xfe"
    text = read_sd(source)
    span = block_span(text, "ESD_TSRESeats")
    if span is None:
        sys.exit("%s has no ESD_TSRESeats" % source)
    inner = text[span[0]:span[1]]
    wanted = {(("door" if n.lower().startswith("d") else "seat"), int(n.lstrip("dD"))) for n in numbers}
    gone = set()

    def out(m):
        key = (m.group(1).lower(), int(m.group(2)))
        if key in wanted:
            gone.add(key)
            return ""
        return m.group(0)
    inner = re.sub(r"^[ \t]*(Seat|Door)[ \t]+(\d+)[ \t]*\([^()]*\)[ \t]*\r?\n", out, inner, flags=re.M | re.I)
    archived(archive, root, [sd], backup=True)
    text = text[:span[0]] + inner + text[span[1]:]
    target = staged(sd)
    os.makedirs(os.path.dirname(target) or ".", exist_ok=True)
    with open(target, "wb") as f:
        f.write(b"\xff\xfe" + text.encode("utf-16-le") if utf16 else text.encode("utf-8"))
    archived(archive, root, [sd])
    left = sd_seats(model)
    name = lambda k: ("d%d" if k[0] == "door" else "%d") % k[1]  # noqa: E731
    print("%s: %s taken out, %d seats and %d doors left%s" % (
        target, ",".join(name(k) for k in sorted(gone)), len(left["seats"]), len(left["doors"]),
        "; not found: %s" % ",".join(name(k) for k in sorted(wanted - gone)) if wanted - gone else ""))


def set_aisles(model, aisles, root, archive):
    """A model's Aisle lines replaced by those given ([x, y, z0, z1] each),
    numbered from 1, its other lines kept (task humans 12: trimming an aisle
    is giving it again with its new ends). The content's .sd in
    <archive>-backup.zip, the written (or staged) one in <archive>.zip."""
    sd = sd_path(model)
    source = sd_read(model)
    if not os.path.exists(source):
        sys.exit("%s has no .sd" % model)
    raw = open(source, "rb").read()
    utf16 = raw[:2] == b"\xff\xfe"
    text = read_sd(source)
    span = block_span(text, "ESD_TSRESeats")
    if span is None:
        sys.exit("%s has no ESD_TSRESeats" % source)
    newline = "\r\n" if "\r\n" in text else "\n"
    inner = re.sub(r"^[ \t]*Aisle\b[^()\n]*\([^()]*\)[ \t]*\r?\n", "", text[span[0]:span[1]], flags=re.M | re.I)
    first = inner.find("\n") + 1
    inner = inner[:first] + "".join(line + newline for line in aisle_lines(aisles)) + inner[first:]
    archived(archive, root, [sd], backup=True)
    text = text[:span[0]] + inner + text[span[1]:]
    target = staged(sd)
    os.makedirs(os.path.dirname(target) or ".", exist_ok=True)
    with open(target, "wb") as f:
        f.write(b"\xff\xfe" + text.encode("utf-16-le") if utf16 else text.encode("utf-8"))
    archived(archive, root, [sd])
    written = sd_seats(model)
    joins = aisle_joins(written["aisles"])
    print("%s: %d aisles (%s), %d joins%s" % (
        target, len(written["aisles"]), ", ".join("A%d" % a[4] for a in written["aisles"]), len(joins),
        "".join("; A%d %s - A%d %s" % (written["aisles"][i][4], fmt(za), written["aisles"][j][4], fmt(zb))
                for i, j, za, zb in joins)))


# --- Scanning ----------------------------------------------------------------
def read_text(path):
    raw = open(path, "rb").read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16")
    for enc in ("utf-8", "cp1250", "latin-1"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            pass


def existing(folder, name):
    """A file named in a vehicle file, found in its folder (by exact name,
    else ignoring case)."""
    name = name.strip('"').replace("\\", "/")
    path = os.path.normpath(os.path.join(folder, name))
    if os.path.exists(path):
        return path
    parent, base = os.path.split(path)
    if os.path.isdir(parent):
        for f in os.listdir(parent):
            if f.lower() == base.lower():
                return os.path.join(parent, f)
    return None


def msts_vehicle(path, depth=0):
    """An .eng/.wag's text with its Include ( "..." ) files put in."""
    text = read_text(path) or ""
    if depth > 3:
        return text

    def inc(m):
        f = existing(os.path.dirname(path), m.group(1))
        return msts_vehicle(f, depth + 1) if f else ""
    return re.sub(r'(?i)\binclude\s*\(\s*"?([^")]+?)"?\s*\)', inc, text)


def msts_models(path):
    """(model, kind) of an .eng/.wag: its WagonShape, its freight
    animations' shapes."""
    folder = os.path.dirname(path)
    out = []

    def walk(lst):
        for i, item in enumerate(lst):
            if isinstance(item, list):
                walk(item)
                continue
            if i + 1 >= len(lst) or not isinstance(lst[i + 1], list) or not lst[i + 1]:
                continue
            body, name = lst[i + 1], item.lower()
            if name == "wagonshape" and isinstance(body[0], str):
                out.append((body[0], "shape"))
            elif name == "freightanim" and isinstance(body[0], str):
                out.append((body[0], "freight animation"))
            elif name == "freightanimstatic":
                for j, x in enumerate(body[:-1]):
                    if isinstance(x, str) and x.lower() == "shape" and isinstance(body[j + 1], list) and body[j + 1]:
                        out.append((body[j + 1][0], "freight animation"))
    walk(tree(msts_vehicle(path)))
    return [(existing(folder, name), kind, name) for name, kind in out]


def maszyna_text(path, depth=0):
    """An .mmd's text without comments, its `include file params end` put in
    (params as (p1), (p2) ...)."""
    text = re.sub(r"//[^\n]*", "", read_text(path) or "")
    if depth > 2:
        return text

    def inc(m):
        f = existing(os.path.dirname(path), m.group(1))
        if not f:
            return ""
        t = maszyna_text(f, depth + 1)
        for i, param in enumerate(m.group(2).split(), 1):
            t = t.replace("(p%d)" % i, param)
        return t
    return re.sub(r"(?is)\binclude\s+(\S+)\s+(.*?)\bend\b", inc, text)


def maszyna_models(path):
    """(model, kind) of an .mmd: its models, lowpoly interior and
    attachments (.e3d; a .t3d text model is not read)."""
    text = maszyna_text(path)
    folder = os.path.dirname(path)
    out = []

    def model(name, kind):
        stem = os.path.splitext(name.strip().rstrip("#"))[0]
        for ext in (".e3d", ".t3d"):
            f = existing(folder, stem + ext)
            if f:
                out.append((f, kind, name))
                return
        out.append((None, kind, name))
    m = re.search(r"(?i)\bmodels:\s*(\S+)", text)
    if m:
        model(m.group(1), "shape")
    m = re.search(r"(?i)\blowpolyinterior:\s*(\S+)", text)
    if m:
        model(m.group(1), "interior")
    m = re.search(r"(?is)\battachments:\s*\{(.*?)\}", text)
    if m:
        for name in m.group(1).split():
            model(name, "attachment")
    return out


def scan_one(job):
    model, tsre, plans_dir, base = job
    row = {"model": model}
    try:
        tris = triangles(model, tsre)
        row["triangles"] = len(tris)
        found = find(tris)
    except Exception as e:  # a model the readers cannot take
        row["problem"] = "not read: " + repr(e)[:100]
        return row
    doors = find_doors(tris) if model.lower().endswith(".s") else []
    row["doors"] = len(doors)
    if "problem" in found:
        row["problem"] = found["problem"]
    else:
        row.update({"floor": found["floor"], "aisle": found["aisle"], "aisleWidth": found["aisleWidth"],
                    "benches": found["benches"], "seats": len(found["seats"]), "passages": len(found["passages"]),
                    "cushion": round(found["seats"][0][1] - found["floor"], 2) if found["seats"] else None,
                    "found": found})
    if ("floor" in found or doors) and plans_dir:
        row["plan"] = plan_name(model, base)
        found["doors"] = doors
        plan(tris, found, os.path.join(plans_dir, row["plan"]),
             "%s: %s, %d doors" % (os.path.relpath(model, base), found.get("problem") or "%d seats" % len(found["seats"]),
                                   len(doors)))
    return row


def scan(folders, out, tsre, maszyna, min_seats, jobs):
    """The cars of vehicle folders: each model once, the seats found in it,
    a plan of each with a floor; report.md and found.json in out."""
    base = os.path.commonpath([os.path.abspath(f) for f in folders]) if len(folders) > 1 else os.path.abspath(folders[0])
    if os.path.isfile(base):
        base = os.path.dirname(base)
    vehicles = []
    for folder in folders:
        patterns = ["*.mmd"] if maszyna else ["*.eng", "*.wag"]
        for pattern in patterns:
            vehicles += glob.glob(os.path.join(folder, "**", pattern), recursive=True)
            vehicles += glob.glob(os.path.join(folder, "**", pattern.upper()), recursive=True)
    vehicles = sorted(set(vehicles))
    models = collections.OrderedDict()
    missing, unread = collections.Counter(), []
    for v in vehicles:
        for model, kind, name in (maszyna_models(v) if maszyna else msts_models(v)):
            if model is None:
                missing[name] += 1
                continue
            entry = models.setdefault(model, {"used": 0, "kinds": set(), "vehicles": []})
            entry["used"] += 1
            entry["kinds"].add(kind)
            if len(entry["vehicles"]) < 3:
                entry["vehicles"].append(os.path.relpath(v, base))
    plans_dir = os.path.join(out, "plans")
    os.makedirs(plans_dir, exist_ok=True)
    jobs_list, rows = [], []
    for model, entry in models.items():
        if CAB.search(os.path.basename(model)):
            rows.append({"model": model, "problem": "a cab's model (left out)"})
        elif model.lower().endswith(".t3d"):
            rows.append({"model": model, "problem": "a .t3d text model (not read)"})
        else:
            jobs_list.append((model, tsre, plans_dir, base))
    with multiprocessing.Pool(jobs) as pool:
        for i, row in enumerate(pool.imap_unordered(scan_one, jobs_list), 1):
            rows.append(row)
            if i % 50 == 0:
                print("%d/%d models" % (i, len(jobs_list)), file=sys.stderr)
    for row in rows:
        entry = models[row["model"]]
        row.update({"used": entry["used"], "kinds": sorted(entry["kinds"]), "vehicles": entry["vehicles"]})
    rows.sort(key=lambda r: (-r["used"], r["model"]))
    json.dump([{k: v for k, v in r.items()} for r in rows], open(os.path.join(out, "found.json"), "w"), indent=1)

    rel = lambda p: os.path.relpath(p, base)  # noqa: E731
    seated = [r for r in rows if "seats" in r and r["seats"] >= min_seats]
    few = [r for r in rows if "seats" in r and r["seats"] < min_seats]
    failed = [r for r in rows if "problem" in r]
    lines = ["# Car seats scan", "",
             "Folders: %s" % ", ".join("`%s`" % f for f in folders),
             "", "- Vehicle files: %d (%s)" % (len(vehicles), ".mmd" if maszyna else ".eng/.wag"),
             "- Models: %d, each read once" % len(rows),
             "- Seats found (%d or more): %d models" % (min_seats, len(seated)),
             "- Fewer seats: %d models" % len(few),
             "- None: %d models" % len(failed),
             "- Door parts (DOOR_A..DOOR_F): %d models, %d of them without seats" % (
                 len([r for r in rows if r.get("doors")]), len([r for r in failed if r.get("doors")]))]
    if missing:
        lines.append("- Models named but not found: %d (%s)" % (sum(missing.values()), ", ".join(
            "%s x%d" % (n, c) for n, c in missing.most_common(5))))
    lines += ["", "## Seats found", "",
              "Check each plan before writing: numbers are the seats' as `write` numbers them.", "",
              "| Model | Vehicles | Kind | Seats | Benches | Cushion | Aisle | Passages | Doors | Plan |",
              "|---|---|---|---|---|---|---|---|---|---|"]
    for r in seated + few:
        lines.append("| `%s` | %d | %s | %d | %d | %.2f m | %.2f m | %d | %d | [plan](plans/%s) |" % (
            rel(r["model"]), r["used"], ", ".join(r["kinds"]), r["seats"], r["benches"], r["cushion"] or 0.0,
            r["aisleWidth"], r["passages"], r.get("doors", 0), r.get("plan", "")))
    with_doors = [r for r in failed if r.get("doors")]
    if with_doors:
        lines += ["", "## Doors without seats", "",
                  "Models with door parts but no seats found (`write` gives them their doors only).", "",
                  "| Model | Vehicles | Doors | Why no seats | Plan |", "|---|---|---|---|---|"]
        for r in with_doors:
            lines.append("| `%s` | %d | %d | %s | %s |" % (rel(r["model"]), r["used"], r["doors"], r["problem"],
                                                       "[plan](plans/%s)" % r["plan"] if r.get("plan") else ""))
    lines += ["", "## No seats", "", "| Why | Models | Most used |", "|---|---|---|"]
    why = collections.defaultdict(list)
    for r in failed:
        key = r["problem"] if not r["problem"].startswith("not read") else "not read"
        if key.startswith("no aisle between"):
            key = "no aisle between the benches"
        why[key].append(r)
    for key, group in sorted(why.items(), key=lambda kv: -len(kv[1])):
        top = ", ".join("`%s` (%d)" % (rel(r["model"]), r["used"]) for r in group[:5])
        lines.append("| %s | %d | %s |" % (key, len(group), top))
    open(os.path.join(out, "report.md"), "w").write("\n".join(lines) + "\n")
    print("%s: %d vehicle files, %d models: %d with seats, %d fewer than %d, %d none" % (
        os.path.join(out, "report.md"), len(vehicles), len(rows), len(seated), len(few), min_seats, len(failed)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["scan", "find", "plan", "write", "doors", "drop", "aisles", "archive"])
    ap.add_argument("paths", nargs="*", help="models; scan: vehicle folders; drop: a model, then seat numbers "
                                              "(12) and door numbers (d2); aisles: a model, then its aisles "
                                              "(\"x y z0 z1\" each); archive: none (every staged .sd)")
    ap.add_argument("--tsre", default=TSRE)
    ap.add_argument("--json")
    ap.add_argument("--out", help="scan, plan: the folder for the report and plans")
    ap.add_argument("--sd", action="store_true", help="plan: the seats of the model's .sd, as written")
    ap.add_argument("--maszyna", action="store_true", help="scan: MaSzyna's .mmd files")
    ap.add_argument("--root")
    ap.add_argument("--archive")
    ap.add_argument("--plans", help="write: numbered plans of the written .sd files here")
    ap.add_argument("--min-seats", type=int, default=8)
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--readme", help="write: a README.md put in the archive")
    ap.add_argument("--stage", help="write, doors, drop: the .sd files into this folder by their paths under --root, "
                                    "not into the content; plan --sd reads them there")
    a = ap.parse_args()
    if a.stage:
        if not a.root:
            ap.error("--stage needs --root")
        STAGE.update({"root": os.path.abspath(a.root), "dir": os.path.abspath(a.stage)})
    if not a.paths and a.command != "archive":
        ap.error("%s needs paths" % a.command)
    if a.command == "archive":
        if not a.stage or not a.archive:
            ap.error("archive needs --stage, --root and --archive")
        archive_stage(a.root, a.archive, a.readme)
    elif a.command == "scan":
        if not a.out:
            ap.error("scan needs --out")
        scan(a.paths, a.out, a.tsre, a.maszyna, a.min_seats, a.jobs)
    elif a.command == "plan":
        os.makedirs(a.out or ".", exist_ok=True)
        for model in a.paths:
            tris = triangles(model, a.tsre)
            found = sd_seats(model) if a.sd else find(tris)
            if found is None:
                print("%s: no ESD_TSRESeats in its .sd" % model)
                continue
            if not a.sd:
                found["doors"] = find_doors(tris) if model.lower().endswith(".s") else []
            # Named by the model's path under --root (models of one name in
            # two folders), else by its name.
            out = os.path.join(a.out or ".", plan_name(model, a.root))
            name = os.path.relpath(os.path.abspath(model), os.path.abspath(a.root)) if a.root else os.path.basename(model)
            plan(tris, found, out, "%s: %s" % (name, found.get("problem") or "%d seats, %d doors%s" % (
                len(found["seats"]), len(found.get("doors", [])), " (its .sd)" if a.sd else "")))
            print(out)
    elif a.command == "find":
        out = []
        for model in a.paths:
            try:
                found = find(triangles(model, a.tsre))
            except Exception as e:
                found = {"problem": repr(e)[:200]}
            found["model"] = model
            out.append(found)
            if not a.json:
                seats = found.get("seats", [])
                headings = collections.Counter(s[3] for s in seats)
                print("%s: %s" % (os.path.basename(model), found.get("problem") or
                      "floor %.2f, aisle %s (%.2f m wide), %d benches, %d seats %s, %d passages at z %s" %
                      (found["floor"], found["aisle"], found["aisleWidth"], found["benches"], len(seats),
                       dict(headings), len(found["passages"]), [p[2] for p in found["passages"]])))
        if a.json:
            json.dump(out, open(a.json, "w"), indent=1)
    elif a.command == "drop":
        if not a.root or not a.archive or len(a.paths) < 2:
            ap.error("drop needs a model, seat numbers, --root and --archive")
        if not all(re.match(r"^[dD]?\d+$", n) for n in a.paths[1:]):
            ap.error("drop: seat numbers (12) and door numbers (d2)")
        drop(a.paths[0], a.paths[1:], a.root, a.archive)
    elif a.command == "aisles":
        if not a.root or not a.archive or len(a.paths) < 2:
            ap.error("aisles needs a model, its aisles (\"x y z0 z1\" each), --root and --archive")
        try:
            aisles = [[float(v) for v in spec.split()] for spec in a.paths[1:]]
        except ValueError:
            aisles = []
        if not aisles or any(len(v) != 4 for v in aisles):
            ap.error("aisles: each aisle as \"x y z0 z1\"")
        set_aisles(a.paths[0], [[v[0], v[1], min(v[2], v[3]), max(v[2], v[3])] for v in aisles], a.root, a.archive)
    elif a.command == "doors":
        if not a.root or not a.archive:
            ap.error("doors needs --root and --archive")
        doors(a.paths, a.root, a.archive, a.tsre, a.readme, a.plans)
    else:
        if not a.root or not a.archive:
            ap.error("write needs --root and --archive")
        write(a.paths, a.root, a.archive, a.tsre, a.min_seats, a.readme, a.plans)


if __name__ == "__main__":
    main()
