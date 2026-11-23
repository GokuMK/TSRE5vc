#!/usr/bin/env python3
"""Sitting places and doors of cars for TSRE's people (ESD_TSRESeats in a
model's .sd): the car's Aisle, its Passages (partitions' closed doors across
the aisle), its EndPassages (doors in its end walls to the next car: TSRE
task characters 05), its numbered Seats and its numbered Doors, found in the
model's most detailed level (an MSTS .s through TSRE's converter, a MaSzyna .e3d by
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
  stairs: car_seats.py stairs <model> ... --root <game root> --archive <prefix>
         [--plans <dir>] [--stage <dir>]   only the Aisle lines, with the
                                           stairs of its level changes, and
                                           Passages where walls cross them
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
  gangways: car_seats.py gangways <model> ... --root <game root> --archive
         <prefix> [--plans <dir>] [--stage <dir>]   EndPassage lines where
                                              an aisle reaches an end wall
                                              (an end wall's Passage
                                              becomes one), its other lines
                                              kept
  axes:  car_seats.py axes <model> ... --root <game root> --archive <prefix>
         [--stage <dir>]                      ESD_TSREAxes ( glTF ) put into
                                              .e3d models' .sd written before
                                              it (their places in the
                                              model's own space)
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
closed): a Passage, as is a wall across stairs (a doorway drawn
see-through on it, a closed door). Parts named for a cab and models named for one are
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
BOM and CRLF, as the route's.

Axes (TSRE task shapes 20): an .sd's places are in MSTS axes (an S file's:
+x the model's right) unless it says ESD_TSREAxes ( glTF ) (+x the model's
left: an .e3d's and a glTF's own). This tool finds, plans and takes values
in the model's own space; it writes an .e3d's block with ESD_TSREAxes
( glTF ) (unless the .sd holds other places without the line: then in MSTS
axes), reads an .sd in its own axes, and writes values back in them. Seats numbered from 1 (`Seat 12 ( x y z
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


def _find(tris, floor=None, spans=None, aisle_at=None, exclude=(), cushion=CUSHION, same=0.04):
    """The car's floor, aisle, passages and seats (its model's space). On a
    floor level of its own (find()): its floor height, the stretches along
    the car it covers the aisle's line in (spans: its cushions only there),
    the main floor's aisle line (its own where it has benches either side)
    and the other levels' heights (faces there are floors, no cushions).
    `cushion`: cushion tops' heights above the floor; `same`: the floor
    along the aisle is the faces within this of its height (a lower deck's
    ramps and vestibules: lower_deck())."""
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
        return any(cushion[0] < y - h < cushion[1] and abs(y - h - CUSHION_USUAL) < mine for h in exclude)

    # Cushions, grouped by side and along the car into benches.
    benches = []
    for t, part, n, a, c in faces:
        y = c[1] - floor
        if CAB.search(part or ""):
            continue  # a cab's seats are the crew's
        if n[1] > 0.85 and cushion[0] < y < cushion[1] and a > 0.002 and inside(c, 0.04) \
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
        if n[1] > 0.95 and all(abs(p[1] - floor) < same for p in t):
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


def walked_on(tris, saloons, floor, same, stretches, sill=None):
    """A floor's stretches along its aisle lines (floor_runs_on()) grown over
    the floor within `same` of its height they run on into, flat or sloping
    (a lower deck's ramps up to its doors' vestibules), and over the floor
    at its doors' thresholds (`sill`, within LEVEL_STEP: a vestibule a step
    below it), gaps of up to two cells bridged as floor_runs() bridges them;
    such floor apart from them (under a raised end) not taken."""
    bands = [(floor, same)] + ([(sill, LEVEL_STEP)] if sill is not None else [])
    cells = set()
    for t, part in tris:
        n, a = normal_area(t)
        if a <= 0 or n[1] <= 0.95 or not any(all(abs(p[1] - h) <= d for p in t) for h, d in bands):
            continue
        for x, w, z0, z1 in saloons:
            if min(p[0] for p in t) > x or max(p[0] for p in t) < x:
                continue
            lo, hi = max(min(p[2] for p in t), z0), min(max(p[2] for p in t), z1)
            for j in range(int(math.floor(lo / CELL)), int(math.ceil(hi / CELL))):
                if covers(t, x, (j + 0.5) * CELL):
                    cells.add(j)
    grown = []
    for z0, z1 in stretches:
        lo, hi = int(round(z0 / CELL)), int(round(z1 / CELL)) - 1
        while any(lo - k in cells for k in (1, 2, 3)):
            lo -= next(k for k in (1, 2, 3) if lo - k in cells)
        while any(hi + k in cells for k in (1, 2, 3)):
            hi += next(k for k in (1, 2, 3) if hi + k in cells)
        grown.append([min(z0, lo * CELL), max(z1, (hi + 1) * CELL)])
    out = []
    for z0, z1 in sorted(grown):
        if out and z0 <= out[-1][1]:
            out[-1][1] = max(out[-1][1], z1)
        else:
            out.append([z0, z1])
    return [(round(z0, 3), round(z1, 3)) for z0, z1 in out]


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


# A double-deck car whose doors open onto its lower deck (the user,
# 2026-11-18: the Gorlitz cars, the modern layout; the Bhp's open onto its
# ends between the decks): an upper deck found (upper_deck()) and its doors'
# thresholds below the usual floors (DOORS_LOW, _find()'s least). Its lower
# deck the car's floor: the floor along the car's middle line with the most
# area within LOWER_REACH of the thresholds in height, the floor up to
# LEVEL's least from it (a ramp up to the doors' vestibules) one floor with
# it, its ends' middle levels its levels; the upper deck left out (the
# user, 2026-11-18: the lower deck as a start). The stairs between its
# levels: connect_levels(), as any car's.
DOORS_LOW = 0.6
LOWER_REACH = 0.3
# A lower deck without cushions CUSHION over its floor: these.
LOWER_CUSHION = (0.25, 0.58)


def lower_deck(tris, doors, deck):
    """A double-deck car's lower deck where its doors open onto it (doors:
    [x, y, z, width] each, y the threshold; deck: the upper deck's height):
    find_main()'s result there (a problem where no seats are found on it),
    None where its doors are not there."""
    sills = sorted(d[1] for d in doors)
    if not sills or sills[len(sills) // 2] >= DOORS_LOW:
        return None
    sill = sills[len(sills) // 2]
    floors = [(a, y) for y, a, runs in floor_runs(tris, 0.0)
              if abs(y - sill) <= LOWER_REACH and y < deck - DECK_RISE]
    if not floors:
        return {"problem": "doors onto a lower deck, no floor found there"}
    floor = max(floors)[1]
    found = find_main(tris, floor=floor, exclude=[deck], same=LEVEL[0], apart=True, sill=sill)
    if found.get("problem") == "no cushions":
        # Its cushions lower over its floor than a usual car's (the
        # Gorlitz88-OR's lower deck's 0.28 m, its floor drawn high).
        found = find_main(tris, floor=floor, exclude=[deck], cushion=LOWER_CUSHION, same=LEVEL[0], apart=True,
                          sill=sill)
    if "problem" not in found:
        # Its seats beyond its aisle's ends: the first treads of the stairs
        # up to its ends' levels (a tread at a seat's height, its riser a
        # backrest: every Gorlitz car's).
        low = [a for a in found["aisles"] if abs(a[1] - found["floor"]) <= LEVEL_STEP]
        z0, z1 = min(a[2] for a in low), max(a[3] for a in low)
        found["seats"] = [q for q in found["seats"] if q[1] - found["floor"] > LOWER_CUSHION[1] + 0.1
                          or z0 <= q[2] <= z1]
        found["lower"] = round(found["floor"], 3)
    return found


# A double-deck car's lower deck (the user's design, 2026-11-18: the Bhp's,
# down side stairs from its entrance, a bench across the aisle's line under
# its stairs up): its floor at least BELOW_DROP below the doors' floor and
# above BELOW_LEAST; benches across the middle line (BENCH_ACROSS of the line
# covered either side) are seats of their own, the aisle found between them.
BELOW_DROP = 0.4
BELOW_LEAST = 0.15
BENCH_ACROSS = 0.2


def once(seats):
    """Seats each place once: of those within half a place across, 0.15 m
    along and 0.05 m in height of each other, facing alike, the first."""
    out = []
    for q in seats:
        if not any(abs(q[0] - p[0]) < PLACE / 2 + 0.03 and abs(q[2] - p[2]) < 0.15 and abs(q[1] - p[1]) < 0.05
                   and q[3] == p[3] for p in out):
            out.append(q)
    return out


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
    seats = []
    for b0, b1, top, w0, w1 in benches:
        # Facing the deck's middle, its back to the car's end.
        z = (b0 + b1) / 2
        heading = 0.0 if z < middle else 180.0
        for x in places_across([], w0, w1):
            seats.append([round(x, 3), round(top, 3), round(z, 3), heading])
    # The deck's own, but a bench's places it found again (the VD Bhp's
    # bench cushions are two overlapping faces: 7 places on a bench of 3,
    # two people on one place: the user's report, 2026-11-19).
    seats = once(seats + list(deck["seats"]))
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
# A face in the way: within this of the line across (the walker's middle;
# seats count at BODY), this long along the car at least (not a door's
# fitting, a stringer's edge: the Gorlitz interiors' side stairs).
OBSTACLE_ACROSS = 0.1
OBSTACLE_ALONG = 0.1
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
            if b[1] < x - OBSTACLE_ACROSS or b[0] > x + OBSTACLE_ACROSS or b[5] < lo or b[4] > hi:
                continue
            if abs(b[6]) < 0.5 or b[5] - b[4] < OBSTACLE_ALONG:
                continue  # walls along the way, partitions' frames, their fittings
            f = floor_at(min(max(b[8], lo), hi))
            if b[6] > 0.5 and b[2] - 0.15 <= f <= b[3] + 0.15:
                continue  # the way's own floor (stairs drawn as one sloped face)
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
    """Stairs written between found aisles at a level change (LEVEL_STEP to
    STAIRS_RISE apart in height, not meeting as a step of ONE_STEP at most,
    one ending where the other begins along the car, STAIRS_RUN times the
    rise apart at most): a sloped aisle each, on a
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
            if not LEVEL_STEP < rise <= STAIRS_RISE:
                continue
            if rise <= ONE_STEP + STEP_TOLERANCE and min(low[3], high[3]) - max(low[2], high[2]) >= -JOIN_GAP:
                continue  # they meet: a step, joined as it is
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
    more = stairs_passages(tris, out, found.get("passages", []))
    if more:
        found["passages"] = sorted(found.get("passages", []) + more, key=lambda p: p[2])
        if log is not None:
            log.append("walls across stairs: Passages at z %s"
                       % ", ".join("%.2f (x %.2f, floor %.2f)" % (p[2], p[0], p[1]) for p in more))
    return found


# Walls across stairs (task characters 03, the user's choice 2026-11-22): a
# wall facing along the car over a sloped aisle's line, from knee to head
# height above the stairs' floor there, is a Passage as a partition over a
# level aisle is (the Bhp's upper saloons' end walls: one quad across the
# car, the doorway onto the stairs see-through in its texture, solid for
# TSRE's characters). Looked at STAIRS_ACROSS either side of the line and
# WALL_BAND above the stairs' floor, faces within 0.1 m along the car
# together: a wall where they cover STAIRS_WALL of it or more.
STAIRS_ACROSS = 0.2
WALL_BAND = (0.3, 1.7)
STAIRS_WALL = 0.5
STAIRS_PASSAGE = 0.8  # its width (m), as a level aisle's widest


def covers_across(t, x, y):
    """Whether a triangle, seen along the car, covers the point (x, y)."""
    (ax, ay), (bx, by), (cx, cy) = [(p[0], p[1]) for p in t]
    d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if abs(d) < 1e-12:
        return False
    l1 = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / d
    l2 = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / d
    return l1 >= -1e-6 and l2 >= -1e-6 and 1 - l1 - l2 >= -1e-6


def stairs_passages(tris, aisles, passages=()):
    """Passages ([x, the stairs' floor there, z, width]) where walls cross
    the sloped aisles among `aisles`; none within 0.25 m along the car of
    one in `passages` (a level aisle's partition) or found before."""
    stairs = [a for a in aisles if sloped(a)]
    if not stairs:
        return []
    walls = []
    for t, _ in tris:
        n, area = normal_area(t)
        if abs(n[2]) > 0.9 and area > 0.0005:
            walls.append((t, sum(p[2] for p in t) / 3, area))
    across = [STAIRS_ACROSS * k / 2 for k in (-2, -1, 0, 1, 2)]
    steps = int(round((WALL_BAND[1] - WALL_BAND[0]) / 0.1))
    heights = [WALL_BAND[0] + (WALL_BAND[1] - WALL_BAND[0]) * k / steps for k in range(steps + 1)]
    out = []
    for a in stairs:
        x = a[0]
        bins = collections.defaultdict(list)
        for t, z, area in walls:
            if not a[2] - 0.05 <= z <= a[3] + 0.05:
                continue
            if max(p[0] for p in t) < x - STAIRS_ACROSS or min(p[0] for p in t) > x + STAIRS_ACROSS:
                continue
            bins[round(z / 0.1)].append((t, z, area))
        for key in sorted(bins):
            near = bins[key] + bins.get(key - 1, []) + bins.get(key + 1, [])
            z = sum(w[1] * w[2] for w in near) / sum(w[2] for w in near)
            f = y_at(a, z)
            covered = sum(1 for dx in across for h in heights
                          if any(covers_across(t, x + dx, f + h) for t, _, _ in near))
            if covered < STAIRS_WALL * len(across) * len(heights):
                continue
            if any(abs(p[2] - z) < 0.25 and abs(p[0] - x) < 0.5 for p in list(passages) + out):
                continue
            out.append([round(x, 3), round(f, 3), round(z, 3), STAIRS_PASSAGE])
    return sorted(out, key=lambda p: p[2])


# Gangways between cars (TSRE task characters 05; the user's design,
# 2026-11-23): an EndPassage, a door in an end wall, where a level aisle
# reaches the car's end. TSRE opens it while the car is coupled to one with
# an EndPassage at the facing end, their floors at most a step apart. Its
# wall at most END_WALL from the model's end (buffers included; TSRE takes
# 1.5 m from the coupling end at most), the aisle's end at most END_AISLE
# from it either side (it reaches the wall; an aisle drawn through it). A
# room closed by a wall less than END_ROOM_DEEP inside it is a cab (no
# EndPassage) where a cab's part (CAB) is in it, or where the vehicle has a
# cab (cab_vehicle()) and no passenger door opens into the room (a
# vestibule) and it holds CAB_PLACES seats at most. A Passage the aisle had
# on that wall becomes the EndPassage (a Passage opens its wall coupled or
# not).
END_WALL = 1.3
END_AISLE = 0.7
END_ROOM_DEEP = 3.0
END_WIDTH = 0.8


def walls_across(tris, x, floor, z0, z1):
    """Walls across a line along the car at x between z0 and z1: where faces
    across the car cover at least STAIRS_WALL of a door's band
    (STAIRS_ACROSS either side of the line, WALL_BAND over the floor); their
    z (a wall's faces within 0.25 m one wall), sorted."""
    across = [x + STAIRS_ACROSS * k / 2 for k in (-2, -1, 0, 1, 2)]
    steps = int(round((WALL_BAND[1] - WALL_BAND[0]) / 0.1))
    heights = [floor + WALL_BAND[0] + (WALL_BAND[1] - WALL_BAND[0]) * k / steps for k in range(steps + 1)]
    bins = collections.defaultdict(list)
    for t, _ in tris:
        n, area = normal_area(t)
        if abs(n[2]) < 0.9 or area < 0.0005:
            continue
        z = sum(p[2] for p in t) / 3
        if not z0 - 0.05 <= z <= z1 + 0.05:
            continue
        if max(p[0] for p in t) < x - STAIRS_ACROSS or min(p[0] for p in t) > x + STAIRS_ACROSS:
            continue
        if max(p[1] for p in t) < heights[0] or min(p[1] for p in t) > heights[-1]:
            continue
        bins[round(z / 0.1)].append((t, z, area))
    out = []
    for key in sorted(bins):
        near = bins[key] + bins.get(key - 1, []) + bins.get(key + 1, [])
        covered = sum(1 for dx in across for h in heights if any(covers_across(t, dx, h) for t, _, _ in near))
        if covered < STAIRS_WALL * len(across) * len(heights):
            continue
        z = sum(w[1] * w[2] for w in bins[key]) / sum(w[2] for w in bins[key])
        if out and z - out[-1] < 0.25:
            continue
        out.append(z)
    return out


_CABS = {}


def cab_vehicle(model):
    """Whether a vehicle with a cab uses this model: an .eng beside an .s
    naming it its WagonShape with a CabView or an ORTS3DCab. A MaSzyna
    interior: False (its cab is a part named for one: OR_PSK_NEW's and
    MaSzyna's sets of 2026-11-23, every MaSzyna cab end found by its part,
    every doorless end room a coach's); no model: True."""
    if not model:
        return True
    if not model.lower().endswith(".s"):
        return False
    folder, name = os.path.split(os.path.abspath(model))
    if folder not in _CABS:
        cabs = set()
        for eng in glob.glob(os.path.join(folder, "*.eng")) + glob.glob(os.path.join(folder, "*.ENG")):
            text = msts_vehicle(eng)
            if not re.search(r"(?i)\b(cabview|orts3dcab)\s*\(", text):
                continue
            for m in re.finditer(r'(?i)\bwagonshape\s*\(\s*"?([^")\s]+)', text):
                cabs.add(m.group(1).lower())
        _CABS[folder] = cabs
    return name.lower() in _CABS[folder]


def end_passages(tris, found, doors=(), log=None, model=None, inner=None):
    """EndPassages ([x, floor, z, width], the model's space) where a level
    aisle of `found` reaches an end wall (see END_WALL); `doors` the car's
    ([x, y, z, width...]: its .sd's, else its door parts'), `model` its
    path (cab_vehicle()). A wall farther from the end with the floor going
    on past it at the aisle's height is a walk-through room's door (the
    user, 2026-11-23: Gorlitz88's end compartment with a WC, walked through
    to the gangway): a Passage into `inner`, the end wall then looked for
    past it. Notes for each end into `log`."""
    level = [a for a in (found.get("aisles") or ([found["aisle"]] if found.get("aisle") else [])) if not sloped(a)]
    if not level or not tris:
        return []
    zs = [p[2] for t, _ in tris for p in t]
    out = []
    for e in (1, -1):
        end = max(zs) if e > 0 else min(zs)
        name = "front" if e > 0 else "rear"
        # The aisle reaching furthest; of those, the longest (a level
        # beside it ending as far: SN84's middle car).
        a = max(level, key=lambda q: (round(q[3] if e > 0 else -q[2], 2), q[3] - q[2]))
        x, floor = a[0], a[1]
        za = a[3] if e > 0 else a[2]
        if e * (end - za) > END_ROOM_DEEP:
            if log is not None:
                log.append("%s: no EndPassage, its aisle ends %.2f m from the end" % (name, e * (end - za)))
            continue
        lo, hi = sorted((za - e * (END_ROOM_DEEP + END_AISLE), end))
        walls = walls_across(tris, x, floor, lo, hi)
        walls += [p[2] for p in found.get("passages", []) if abs(p[0] - x) < 0.5 and lo <= p[2] <= hi
                  and all(abs(p[2] - w) >= 0.25 for w in walls)]
        walls.sort(key=lambda w: e * w)
        # The floor along the line at the aisle's height (a step either
        # way): upward faces over it.
        ups = []
        for t, _ in tris:
            n, area = normal_area(t)
            if n[1] > 0.85 and area > 0.0005 and min(p[0] for p in t) <= x <= max(p[0] for p in t) \
                    and abs(sum(p[1] for p in t) / 3 - floor) <= ONE_STEP:
                ups.append(t)

        def floor_at(z):
            return any(covers(t, x, z) for t in ups)

        def cab_room(room):
            """A room between two z: a cab (cab_vehicle(), no passenger
            door into it, CAB_PLACES seats at most; or a cab's part in it)."""
            opens = [d for d in doors if d[2] + d[3] / 2 > room[0] and d[2] - d[3] / 2 < room[1]]
            seats = [q for q in found.get("seats", []) if room[0] < q[2] < room[1]]
            part = any(p and CAB.search(p) for t, p in tris if room[0] < sum(q[2] for q in t) / 3 < room[1])
            return part or (not opens and len(seats) <= CAB_PLACES and cab_vehicle(model)), part
        # Outward from the aisle's end (or a wall it was drawn through):
        # walls farther than END_WALL from the end with the floor going on
        # past them are walk-through rooms' doors; the first within it the
        # end wall.
        ahead = [w for w in walls if e * (w - za) >= -END_AISLE]
        crossed, wall, stop = [], None, None
        for i, w in enumerate(ahead):
            if e * (end - w) <= END_WALL:
                wall = w
                break
            if not floor_at(w + e * 0.3):
                stop = "a wall at %.2f, no floor past it" % w
                break
            nxt = ahead[i + 1] if i + 1 < len(ahead) else end
            cab, part = cab_room(sorted((w, nxt)))
            if cab:
                stop = "a cab past its wall at %.2f (%s)" % (w, "a cab's part" if part else "no door")
                break
            crossed.append(w)
        if wall is None and stop is None:
            # An open end: the floor going on to within END_WALL of the end
            # (no farther than the model's end).
            z = crossed[-1] if crossed else za
            while e * (end - z) > 0.05 and floor_at(z + e * 0.1):
                z += e * 0.1
            if e * (end - z) <= END_WALL:
                wall = end if e * (z - end) > 0 else z
            else:
                stop = "no wall within %.1f m of the end" % END_WALL
        if wall is None:
            if log is not None:
                log.append("%s: no EndPassage, %s" % (name, stop))
            continue
        inside = [w for w in walls if 0.3 <= e * (wall - w) <= END_ROOM_DEEP]
        if inside:
            cab, part = cab_room(sorted((inside[-1], wall)))
            if cab:
                if log is not None:
                    log.append("%s: no EndPassage, a cab (a room %.2f m deep behind its wall at %.2f, %s)"
                               % (name, e * (wall - inside[-1]), wall, "a cab's part" if part else "no door"))
                continue
        width = min([END_WIDTH] + [p[3] for p in found.get("passages", [])
                                   if abs(p[2] - wall) < 0.25 and abs(p[0] - x) < 0.5])
        out.append([round(x, 3), round(floor, 3), round(wall, 3), round(width, 2)])
        for w in crossed:
            if inner is not None and not any(abs(p[2] - w) < 0.25 and abs(p[0] - x) < 0.5
                                             for p in found.get("passages", [])):
                inner.append([round(x, 3), round(floor, 3), round(w, 3), END_WIDTH])
        if log is not None:
            log.append("%s: EndPassage at z %.2f (x %.2f, floor %.2f, %.2f m from the end)%s"
                       % (name, wall, x, floor, e * (end - wall),
                          "; a walk-through room, its door a Passage at %s" % ", ".join("%.2f" % w for w in crossed)
                          if crossed else ""))
    return out


def with_end_passages(tris, found, doors=(), log=None, model=None):
    """`found` with its EndPassages (end_passages()), the Passages on their
    walls taken out (each became one), walk-through rooms' doors added as
    Passages."""
    if "problem" in found:
        return found
    inner = []
    ends = end_passages(tris, found, doors, log, model, inner)
    found["endPassages"] = ends
    found["passages"] = sorted([p for p in found.get("passages", [])
                                if not any(abs(p[2] - q[2]) < 0.25 and abs(p[0] - q[0]) < 0.5 for q in ends)]
                               + inner, key=lambda p: p[2])
    return found


def find(tris, doors=None, log=None, model=None):
    """find_levels() with the car's EndPassages (end_passages()): its doors
    (`doors`, or a function giving them) asked for them, its model's path
    (`model`: cab_vehicle())."""
    found = find_levels(tris, doors, log)
    if "problem" in found:
        return found
    listed = doors() if callable(doors) else doors
    return with_end_passages(tris, found, listed or [], log, model)


def find_levels(tris, doors=None, log=None):
    """The car's floor, aisles, passages and seats (its model's space), as
    find_main() finds them; a double-deck car's lower deck where its doors
    open onto it (lower_deck(); `doors`: the car's doors, or a function
    giving them, asked only for a double-deck car); else its upper deck
    (upper_deck()) where it has more seats (the main floor its doors' floor
    between the decks), with its lower deck (deck_below(): the user's
    design, 2026-11-18). Every level change then written as stairs
    (connect_levels())."""
    main = find_main(tris)
    deck = upper_deck(tris, main.get("floor"))
    if deck is not None and doors is not None:
        low = lower_deck(tris, doors() if callable(doors) else doors, deck["deck"])
        if low is not None:
            return low if "problem" in low else connect_levels(tris, low, log)
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


def find_main(tris, floor=None, exclude=(), cushion=CUSHION, same=None, apart=False, sill=None):
    """The car's floor, aisles, passages and seats (its model's space): its
    main floor's (the most area) as _find() finds them; where other floor
    levels cover the aisle's line (a raised end, a low-floor middle, a low
    entrance), each level's seats too, and an aisle for each stretch of each
    level (task humans 12: aisles joined where they meet, TSRE finds the
    joins). A car of one floor: its one aisle as before. A lower deck's
    (lower_deck()): its floor given, the other decks' heights (`exclude`),
    its cushions' heights, the floor within `same` of its height (its
    ramps and vestibules) one floor with it and its aisle on to its doors'
    floor (`sill`), each level's stretches apart (its ends)."""
    base = _find(tris, floor=floor, exclude=exclude, cushion=cushion, same=same or 0.04)
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
    if same:
        main = (main[0], main[1], walked_on(tris, saloons, main[0], same, main[2], sill))
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
                     exclude=[r[0] for r in levels] + list(exclude), cushion=cushion)
        if "problem" not in more:
            seats += more["seats"]
            passages += [p for p in more["passages"] if any(z0 <= p[2] <= z1 for z0, z1 in beyond)]
    found_levels = []
    for y, area, spans in levels:
        spans = trimmed(spans)
        if not spans:
            continue
        # A lower deck's (`apart`): each stretch of a level on its own (a
        # double-deck car's two ends, rooms of their own heights of seats).
        took, count = [], 0
        for group in ([[span] for span in spans] if apart else [spans]):
            middle = sum(z0 + z1 for z0, z1 in group) / (2 * len(group))
            level = _find(tris, floor=y, spans=group, aisle_at=line_at(saloons, middle),
                          exclude=[h for h in heights if h != y] + list(exclude), cushion=cushion)
            if "problem" not in level:
                # A cushion between two floors' heights found from both: the
                # first floor's (the main one's) seat.
                level["seats"] = [q for q in level["seats"] if not any(
                    abs(q[0] - s[0]) < 0.12 and abs(q[2] - s[2]) < 0.12 and abs(q[1] - s[1]) < 0.05 and q[3] == s[3]
                    for s in seats)]
            if "problem" in level or not level["seats"]:
                # No seats there (an entrance's floor a step below the
                # saloon): no aisle either; people step in onto the main
                # floor's, as on a car of one floor.
                continue
            # Its own aisle's line where its benches stand either side (each
            # saloon's, rule 4).
            seats += level["seats"]
            passages += [p for p in level["passages"] if any(z0 <= p[2] <= z1 for z0, z1 in group)]
            aisles += on_saloons(level["saloons"], y, group)
            took += group
            count += len(level["seats"])
        if took:
            found_levels.append({"floor": round(y, 3), "spans": took, "seats": count})
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


def maszyna_doors(model):
    """A MaSzyna interior's car's doors as TSRE reads them (CarDoors): the
    parts named its .mmd's animdoorprefix and a number (1 to 8) in the car's
    model (the .mmd's `models:`, in the interior's space); [x, y, z, width]
    each, y the threshold; [] where no .mmd beside it or up to two folders
    up names it its lowpolyinterior. For finding only (lower_deck()): TSRE
    reads them itself, they are not written."""
    folder = os.path.dirname(os.path.abspath(model))
    for up in (folder, os.path.dirname(folder), os.path.dirname(os.path.dirname(folder))):
        for mmd in sorted(set(glob.glob(os.path.join(up, "*.mmd")) + glob.glob(os.path.join(up, "*.MMD")))):
            models = maszyna_models(mmd)
            if not any(m and kind == "interior" and os.path.samefile(m, model) for m, kind, _ in models):
                continue
            shape = next((m for m, kind, _ in models if kind == "shape" and m and m.lower().endswith(".e3d")), None)
            prefix = re.search(r"(?i)\banimdoorprefix:\s*(\S+)", maszyna_text(mmd))
            if shape is None or prefix is None:
                continue
            names = {(prefix.group(1) + str(i)).lower() for i in range(1, 9)}
            sides = ([], [])
            for t, name in e3d_triangles(shape):
                if name.lower() in names:
                    sides[0 if sum(p[0] for p in t) < 0 else 1].append(t)
            return [d for side in sides for d in sorted(split_doors(side), key=lambda d: d[2])]
    return []


def doors_of(model, tris):
    """A function giving a model's doors for find() (an S file's door parts,
    a MaSzyna interior's car's: maszyna_doors()), read when asked."""
    if model.lower().endswith(".s"):
        return lambda: find_doors(tris)
    if model.lower().endswith(".e3d"):
        return lambda: maszyna_doors(model)
    return None


# --- Checking ----------------------------------------------------------------
# Aisles join as TSRE joins them at load (ShapeSeats::joins, task humans 12):
# where their ranges overlap or leave at most JOIN_GAP between them, their
# floors there at most ONE_STEP apart (a step); a level change of more is a
# sloped aisle of its own (stairs, a ramp: [x, y, z0, z1, number, y1], the
# floor y at z0 and y1 at z1), joined at its ends only (the user's design,
# 2026-11-18: aisles written apart are meant apart).
JOIN_GAP, LEVEL_STEP, ONE_STEP = 0.3, 0.08, 0.25
STEP_TOLERANCE = 0.001  # floors written 0.25 m apart are a step


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
        return zo if abs(y_at(o, zo) - y_at(s, z)) <= ONE_STEP + STEP_TOLERANCE else None
    out = []
    for i in range(len(aisles)):
        for j in range(i + 1, len(aisles)):
            p, q = aisles[i], aisles[j]
            if not sloped(p) and not sloped(q):
                if abs(p[1] - q[1]) > ONE_STEP + STEP_TOLERANCE:
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
    doors magenta on the car's side with their numbers (D1, D2...), end
    passages red with their floors (E 1.26)."""
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
    for p in found.get("endPassages", []):
        # An end door (task characters 05): red, E with its floor.
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#e22" stroke-width="5"/>'
                     % (X(p[2]), Y(p[0] - p[3] / 2), X(p[2]), Y(p[0] + p[3] / 2)))
        parts.append('<text x="%.1f" y="%.1f" font-size="10" fill="#c11" text-anchor="middle">E %.2f</text>'
                     % (X(p[2]), Y(p[0] + p[3] / 2) - 3, p[1]))
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
        lines += end_passage_lines(found.get("endPassages", []))
        for i, s in enumerate(found["seats"]):
            lines.append("\t\tSeat %d ( %s )" % (i + 1, " ".join(fmt(v) for v in s[:4])))
    lines += door_lines(found.get("doors", []))
    lines.append("\t)")
    return lines


def end_passage_lines(ends):
    return ["\t\tEndPassage ( %s )" % " ".join(fmt(v) for v in p[:4]) for p in ends]


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
    "endPassages", "seats" ([x, y, z, heading, number]), "doors" ([x, y, z,
    width, number]), "floor"}; None without one."""
    sd = sd_read(model)
    if not os.path.exists(sd):
        return None
    text = read_sd(sd)
    span = block_span(text, "ESD_TSRESeats")
    if span is None:
        return None
    inner = text[span[0]:span[1]]
    inner = inner[inner.index("(") + 1:]
    out = {"aisles": [], "passages": [], "endPassages": [], "seats": [], "doors": []}
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
        elif name.lower() == "endpassage" and len(values) >= 4:
            out["endPassages"].append(values[:4])
        elif name.lower() == "door" and len(values) >= 4:
            number = int(label) if label.isdigit() else len(out["doors"]) + 1
            out["doors"].append(values[:4] + [number])
    if out["aisles"]:
        out["aisle"] = out["aisles"][0]
        out["floor"] = out["aisles"][0][1]
    # In the model's own space (task shapes 20).
    if text_axes(text) != own_axes(model):
        out = mirrored(out)
    return out


# Task shapes 20: the axes an .sd's places are written in.
AXES_LINE = re.compile(r"^[ \t]*ESD_TSREAxes\b[ \t]*\(([^()]*)\)", re.M | re.I)
PLACED = re.compile(r"^[ \t]*ESD_(TSRELight|TSREEmitter|TSREDisplay|TSRESeats|Bounding_Box|Complex)\b", re.M | re.I)
GLTF_LINE = "\tESD_TSREAxes ( glTF )"


def own_axes(model):
    """A model's own axes: an .s's MSTS, any other model's glTF."""
    return "msts" if os.path.splitext(model)[1].lower() == ".s" else "gltf"


def text_axes(text):
    """An .sd's axes: glTF where it says so, else MSTS."""
    m = AXES_LINE.search(text)
    return "gltf" if m and m.group(1).strip().lower() == "gltf" else "msts"


def mirrored(found):
    """Seats, aisles, passages, end passages and doors (found, or read from
    an .sd) mirrored in x: between an .sd's axes and its model's."""
    out = dict(found)
    out["seats"] = [[-s[0], s[1], s[2], (-s[3]) % 360.0] + list(s[4:]) for s in found.get("seats", [])]
    out["aisles"] = [[-a[0]] + list(a[1:]) for a in found.get("aisles", [])]
    if found.get("aisle"):
        out["aisle"] = [-found["aisle"][0]] + list(found["aisle"][1:])
    out["passages"] = [[-p[0]] + list(p[1:]) for p in found.get("passages", [])]
    out["endPassages"] = [[-p[0]] + list(p[1:]) for p in found.get("endPassages", [])]
    out["doors"] = [[-d[0]] + list(d[1:]) for d in found.get("doors", [])]
    return out


def sd_text(model_path, found):
    """The model's .sd with `found` (in the model's own space) as its
    ESD_TSRESeats block, in the .sd's axes (task shapes 20): a model of glTF
    axes (an .e3d) gets ESD_TSREAxes ( glTF ) where nothing else of it is
    placed, else its block is written in the axes the .sd has."""
    sd = sd_path(model_path)
    own = own_axes(model_path)
    marked = [GLTF_LINE] if own == "gltf" else []
    if os.path.exists(sd_read(model_path)):
        text = read_sd(sd_read(model_path)).replace("\r\n", "\n")
        text = remove_block(text, "ESD_TSRESeats")
        if not marked or AXES_LINE.search(text) or PLACED.search(text):
            marked = []
            axes = text_axes(text)
        else:
            axes = own
        new = marked + block(found if axes == own else mirrored(found))
        close = text.rstrip().rfind(")")
        text = text[:close].rstrip("\n") + "\n" + "\n".join(new) + "\n" + text[close:]
    elif model_path.lower().endswith(".e3d"):
        # Beside a MaSzyna model only the block: its .sd's other lines mean
        # more there (ESD_Alternative_Texture: a season folder).
        text = "\n".join([HEADER, "", "Shape ( %s" % os.path.basename(model_path)] + marked + block(found) + [")", ""])
    else:
        text = "\n".join([HEADER, "", "Shape ( %s" % os.path.basename(model_path), "\tESD_Detail_Level ( 0 )",
                          "\tESD_Alternative_Texture ( 0 )"] + marked + block(found) + [")", ""])
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
            found = find(tris, doors_of(model, tris), model=model)
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


def stairs(models, root, archive, tsre, readme=None, plans_dir=None):
    """The Aisle lines of models' .sd written again with their level
    changes' stairs (connect_levels() over the aisles and seats as written:
    the user's design, 2026-11-18), seats on a floor found hidden under
    another taken out by their lines, a Passage added where a wall crosses
    its stairs (stairs_passages(): task characters 03, 2026-11-22); every
    other line kept as it is (a set's reviewed seats, its passages, its
    doors). An .sd with stairs already keeps its Aisle lines as they are
    (connecting its pieces again would pair them twice): only Passages over
    its stairs added. Models whose aisles do not change and that get no
    Passage are left as they are."""
    plans, skipped = [], []
    for model in models:
        found = sd_seats(model)
        if found is None or not found["aisles"]:
            skipped.append((model, "no aisles in its .sd"))
            continue
        try:
            tris = triangles(model, tsre)
        except Exception as e:  # a model the readers cannot take
            skipped.append((model, repr(e)[:120]))
            continue
        before = [a[:4] + ([0, a[5]] if sloped(a) else []) for a in found["aisles"]]
        numbers = {s[4] for s in found["seats"]}
        written = [list(p) for p in found["passages"]]
        found["aisles"] = [list(a) for a in before]
        log = []
        if any(sloped(a) for a in before):
            added = stairs_passages(tris, before, written)
            found["passages"] = sorted(written + added, key=lambda p: p[2])
            if added:
                log.append("walls across stairs: Passages at z %s"
                           % ", ".join("%.2f (x %.2f, floor %.2f)" % (p[2], p[0], p[1]) for p in added))
        else:
            connect_levels(tris, found, log)
            added = [p for p in found["passages"] if list(p) not in written]
        gone = numbers - {s[4] for s in found["seats"]}
        moved = [list(a) for a in found["aisles"]] != before
        if not moved and not gone and not added:
            skipped.append((model, "no level change to write"))
            continue
        sd = sd_path(model)
        text = read_sd(sd_read(model)).replace("\r\n", "\n")
        span = block_span(text, "ESD_TSRESeats")
        inner = re.sub(r"^[ \t]*Seat[ \t]+(\d+)[ \t]*\([^()]*\)[ \t]*\n",
                       lambda m: "" if int(m.group(1)) in gone else m.group(0), text[span[0]:span[1]],
                       flags=re.M | re.I)
        # Written in the .sd's axes (task shapes 20).
        flip = text_axes(text) != own_axes(model)
        passages = ["\t\tPassage ( %s )" % " ".join(fmt(v) for v in p)
                    for p in (mirrored({"passages": added})["passages"] if flip else added)]
        if moved:
            inner = re.sub(r"^[ \t]*Aisle\b[^()\n]*\([^()]*\)[ \t]*\n", "", inner, flags=re.M | re.I)
            first = inner.find("\n") + 1
            lines = aisle_lines(mirrored(found)["aisles"] if flip else found["aisles"]) + passages
            inner = inner[:first] + "".join(line + "\n" for line in lines) + inner[first:]
        else:
            # After its last Aisle line.
            last = list(re.finditer(r"^[ \t]*Aisle\b[^()\n]*\([^()]*\)[ \t]*\n", inner, flags=re.M | re.I))[-1]
            inner = inner[:last.end()] + "".join(line + "\n" for line in passages) + inner[last.end():]
        text = text[:span[0]] + inner + text[span[1]:]
        plans.append((model, sd, text, found, tris))
        for line in log:
            print("  %s: %s" % (os.path.basename(model), line))
    finish_writing(plans, root, archive, readme, plans_dir)
    for model, why in skipped:
        print("SKIPPED %s: %s" % (os.path.relpath(os.path.abspath(model), os.path.abspath(root)), why))
    print("%d written, %d skipped; %s.zip, %s-backup.zip" % (len(plans), len(skipped), archive, archive))


def gangways(models, root, archive, tsre, readme=None, plans_dir=None):
    """EndPassage lines (end_passages(): task characters 05, the user's
    design 2026-11-23) put into models' .sd written before them, after its
    last Passage or Aisle line, with a Passage on each walk-through end
    room's door; a Passage on an end wall taken out (it became the
    EndPassage); every other line kept. Written in the .sd's axes. Left
    as they are: .sd files without aisles, with EndPassage lines already
    (a second run writes nothing), and models whose aisles reach no end."""
    plans, skipped = [], []
    for model in models:
        found = sd_seats(model)
        if found is None or not found["aisles"]:
            skipped.append((model, "no aisles in its .sd"))
            continue
        if found["endPassages"]:
            skipped.append((model, "EndPassage written already"))
            continue
        try:
            tris = triangles(model, tsre)
        except Exception as e:  # a model the readers cannot take
            skipped.append((model, repr(e)[:120]))
            continue
        doors = found["doors"]
        if not doors:
            parts = doors_of(model, tris)
            doors = (parts() if parts else None) or []
        log = []
        before = [list(p) for p in found["passages"]]
        with_end_passages(tris, found, doors, log, model)
        for line in log:
            print("  %s: %s" % (os.path.basename(model), line))
        if not found["endPassages"]:
            skipped.append((model, "no aisle reaches an end wall"))
            continue
        gone = [p for p in before if p not in found["passages"]]
        added = [p for p in found["passages"] if p not in before]
        sd = sd_path(model)
        text = read_sd(sd_read(model)).replace("\r\n", "\n")
        span = block_span(text, "ESD_TSRESeats")
        inner = text[span[0]:span[1]]
        # Written in the .sd's axes (task shapes 20).
        flip = text_axes(text) != own_axes(model)
        written = mirrored(found) if flip else found
        gone_written = mirrored({"passages": gone})["passages"] if flip else gone

        def is_gone(m):
            values = [float(v) for v in m.group(1).split()]
            return any(len(values) >= 4 and all(abs(values[k] - g[k]) < 1e-3 for k in range(4)) for g in gone_written)
        inner = re.sub(r"^[ \t]*Passage[ \t]*\(([^()]*)\)[ \t]*\n", lambda m: "" if is_gone(m) else m.group(0),
                       inner, flags=re.M | re.I)
        lines = ["\t\tPassage ( %s )" % " ".join(fmt(v) for v in p)
                 for p in (mirrored({"passages": added})["passages"] if flip else added)]
        lines += end_passage_lines(written["endPassages"])
        anchors = list(re.finditer(r"^[ \t]*(Passage|Aisle)\b[^()\n]*\([^()]*\)[ \t]*\n", inner, flags=re.M | re.I))
        at = anchors[-1].end() if anchors else inner.find("\n") + 1
        inner = inner[:at] + "".join(line + "\n" for line in lines) + inner[at:]
        text = text[:span[0]] + inner + text[span[1]:]
        plans.append((model, sd, text, found, tris))
    finish_writing(plans, root, archive, readme, plans_dir)
    for model, why in skipped:
        print("SKIPPED %s: %s" % (os.path.relpath(os.path.abspath(model), os.path.abspath(root)), why))
    print("%d written, %d skipped; %s.zip, %s-backup.zip" % (len(plans), len(skipped), archive, archive))


def axes(models, root, archive):
    """ESD_TSREAxes ( glTF ) put into the .sd of models of glTF axes (.e3d)
    written before TSRE task shapes 20 (2026-11-22): this tool wrote their
    places in the model's own space, and an .sd without the line is read in
    MSTS axes now. The line goes before its ESD_TSRESeats block; its other
    lines are kept. Left as they are: .s models (MSTS, their own), .sd files
    with the line, without a seats block."""
    plans, skipped = [], []
    for model in models:
        source = sd_read(model)
        if own_axes(model) != "gltf":
            skipped.append((model, "an S file's: MSTS, its own"))
            continue
        if not os.path.exists(source):
            skipped.append((model, "no .sd"))
            continue
        text = read_sd(source).replace("\r\n", "\n")
        if AXES_LINE.search(text):
            skipped.append((model, "ESD_TSREAxes written already"))
            continue
        span = block_span(text, "ESD_TSRESeats")
        if span is None:
            skipped.append((model, "no ESD_TSRESeats"))
            continue
        text = text[:span[0]] + GLTF_LINE + "\n" + text[span[0]:]
        plans.append((model, sd_path(model), text, None, None))
    finish_writing(plans, root, archive, None, None)
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
    # Given in the model's space (as its plans), written in the .sd's axes
    # (task shapes 20).
    if text_axes(text) != own_axes(model):
        aisles = mirrored({"aisles": aisles})["aisles"]
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
        found = find(tris, doors_of(model, tris), model=model)
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
    ap.add_argument("command", choices=["scan", "find", "plan", "write", "doors", "drop", "aisles", "stairs", "axes",
                                        "gangways", "archive"])
    ap.add_argument("paths", nargs="*", help="models; scan: vehicle folders; drop: a model, then seat numbers "
                                              "(12) and door numbers (d2); aisles: a model, then its aisles "
                                              "(\"x y z0 z1\" each, \"x y z0 z1 y1\" for stairs); archive: none "
                                              "(every staged .sd)")
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
            found = sd_seats(model) if a.sd else find(tris, doors_of(model, tris), model=model)
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
                tris = triangles(model, a.tsre)
                found = find(tris, doors_of(model, tris), model=model)
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
        if not aisles or any(len(v) not in (4, 5) for v in aisles):
            ap.error("aisles: each aisle as \"x y z0 z1\" (\"x y z0 z1 y1\" for stairs)")
        set_aisles(a.paths[0], [stairs_aisle(v[0], v[1], v[2], v[4], v[3]) if len(v) == 5
                                else [v[0], v[1], min(v[2], v[3]), max(v[2], v[3])] for v in aisles], a.root, a.archive)
    elif a.command == "stairs":
        if not a.root or not a.archive:
            ap.error("stairs needs --root and --archive")
        stairs(a.paths, a.root, a.archive, a.tsre, a.readme, a.plans)
    elif a.command == "gangways":
        if not a.root or not a.archive:
            ap.error("gangways needs --root and --archive")
        gangways(a.paths, a.root, a.archive, a.tsre, a.readme, a.plans)
    elif a.command == "axes":
        if not a.root or not a.archive:
            ap.error("axes needs --root and --archive")
        axes(a.paths, a.root, a.archive)
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
