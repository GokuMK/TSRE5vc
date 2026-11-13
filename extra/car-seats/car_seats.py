#!/usr/bin/env python3
"""Sitting places of cars for TSRE's people (ESD_TSRESeats in a model's .sd):
the car's Aisle, its Passages (partitions' closed doors across the aisle)
and its numbered Seats, found in the model's most detailed level (an MSTS
.s through TSRE's converter, a MaSzyna .e3d by its records), in the model's
space as written (an S file's as its file's, as the lights' positions).
TSRE never guesses seats at load: a car has them once its .sd has them.
The workflow: README.md beside this script.

  scan:  car_seats.py scan <folder> ... --out <dir> [--maszyna]   the cars of
         vehicle folders (.eng/.wag shapes and freight animations; with
         --maszyna .mmd models and interiors): report.md, found.json, plans
  find:  car_seats.py find <model> ... [--json out.json]           a summary
  plan:  car_seats.py plan <model> ... --out <dir> [--sd]           top views
         (SVG): the seats found, or with --sd the model's .sd as written
  write: car_seats.py write <model> ... --root <game root> --archive <prefix>
         [--min-seats 8] [--readme README.md] [--plans <dir>]
  drop:  car_seats.py drop <model> <seat number> ... --root <game root>
         --archive <prefix>                    wrong seats out of its .sd

  Every command takes --tsre <TSRE5vc executable> (default: the build
  folder of the checkout this script is in) for .s files.

How a car is read: the floor is the height with the most upward area inside
the body; cushions are upward faces 0.33 to 0.58 m above it, grouped into
benches (one side of the aisle, next to each other along the car); a
bench's back is the side with its backrest (upright faces above the
cushion), the sitter faces away from it; its places are 0.45 m apart along
the bench. The aisle runs between the benches' inner edges, along the floor
around the car's middle; a partition is a wall across the car blocking the
aisle (its door modelled closed): a Passage. Parts named for a cab and
models named for one are left out.

write: each model's .sd beside it, its other lines kept and an
ESD_TSRESeats block put in (a new .sd: the route's header, ESD_Detail_Level
and ESD_Alternative_Texture 0; beside an .e3d only the block); UTF-16 with
BOM and CRLF, as the route's. Seats numbered from 1 (`Seat 12 ( x y z
heading )`), as on the plans. Before writing, <archive>-backup.zip holds the
.sd files there were; after, <archive>.zip the written ones; both by their
paths under --root, to extract into it. Cars with fewer seats than
--min-seats are left out.
"""
import argparse, collections, glob, json, math, multiprocessing, os, re, struct, subprocess, sys, tempfile, zipfile

PLACE = 0.45          # a place's width on a bench (m)
CUSHION = (0.33, 0.58)  # cushion tops above the floor (m)
HEADER = "SIMISA@@@@@@@@@@JINX0t1t______"
# Parts of a cab (MSTS matrices, MaSzyna parts): no passengers' seats there.
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
                    out.append(([to_root(points[verts[v]], matrix) for v in idx[k:k + 3]], names[matrix]))
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


def find(tris):
    """The car's floor, aisle, passages and seats (its model's space)."""
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
    floors = collections.Counter()
    for t, part, n, a, c in faces:
        if n[1] > 0.95 and 0.6 < c[1] < 1.8 and inside(c, 0.12):
            floors[round(c[1], 2)] += a
    if not floors:
        return {"problem": "no floor"}
    floor = max(floors, key=floors.get)

    # Cushions, grouped by side and along the car into benches.
    benches = []
    for t, part, n, a, c in faces:
        y = c[1] - floor
        if CAB.search(part or ""):
            continue  # a cab's seats are the crew's
        if n[1] > 0.85 and CUSHION[0] < y < CUSHION[1] and a > 0.002 and inside(c, 0.04):
            side = c[0] > 0
            for b in benches:
                if b["side"] == side and abs(b["y"] - y) < 0.08 and b["z0"] - 0.12 < c[2] < b["z1"] + 0.12:
                    b["z0"] = min(b["z0"], min(p[2] for p in t))
                    b["z1"] = max(b["z1"], max(p[2] for p in t))
                    b["x0"] = min(b["x0"], min(p[0] for p in t))
                    b["x1"] = max(b["x1"], max(p[0] for p in t))
                    b["area"] += a
                    b["ys"].append((y, a))
                    break
            else:
                benches.append({"side": side, "y": y, "z0": min(p[2] for p in t), "z1": max(p[2] for p in t),
                                "x0": min(p[0] for p in t), "x1": max(p[0] for p in t), "area": a, "ys": [(y, a)]})
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
    if not plus or not minus:
        return {"problem": "seats on one side only", "floor": floor}
    inner_plus = percentile([b["x0"] for b in plus], 0.2)
    inner_minus = percentile([b["x1"] for b in minus], 0.8)
    aisle_x = (inner_plus + inner_minus) / 2
    aisle_width = inner_plus - inner_minus
    if aisle_width < 0.4:
        return {"problem": "no aisle between the benches (%.2f m)" % aisle_width, "floor": floor}

    # The floor along the aisle: its cells (0.25 m) at the floor's height.
    cells = set()
    for t, part, n, a, c in faces:
        if n[1] > 0.95 and all(abs(p[1] - floor) < 0.04 for p in t):
            if min(p[0] for p in t) <= aisle_x + 0.15 and max(p[0] for p in t) >= aisle_x - 0.15:
                for j in range(int(math.floor(min(p[2] for p in t) / 0.25)), int(math.ceil(max(p[2] for p in t) / 0.25))):
                    cells.add(j)
    if not cells:
        return {"problem": "no floor along the aisle", "floor": floor}
    middle = min(cells, key=lambda j: abs(j * 0.25 + 0.125 - sum(b["z0"] + b["z1"] for b in benches) / (2 * len(benches))))
    lo = hi = middle
    while lo - 1 in cells or lo - 2 in cells:
        lo -= 1 if lo - 1 in cells else 2
    while hi + 1 in cells or hi + 2 in cells:
        hi += 1 if hi + 1 in cells else 2
    aisle = [round(aisle_x, 3), round(floor, 3), round(lo * 0.25, 3), round((hi + 1) * 0.25, 3)]

    # Each bench's back: its backrest, the upright faces above the cushion.
    seats = []
    for b in benches:
        long_x = (b["x1"] - b["x0"]) >= (b["z1"] - b["z0"]) * 0.8
        cx, cz = (b["x0"] + b["x1"]) / 2, (b["z0"] + b["z1"]) / 2
        top = floor + b["y"]
        weight = 0.0
        lean = 0.0
        for t, part, n, a, c in faces:
            if abs(n[1]) > 0.5 or not (top + 0.15 < c[1] < top + 0.75):
                continue
            if long_x:
                if b["x0"] <= c[0] <= b["x1"] and b["z0"] - 0.25 < c[2] < b["z1"] + 0.25 and abs(n[2]) > 0.6:
                    weight += a
                    lean += a * (c[2] - cz)
            else:
                if b["z0"] <= c[2] <= b["z1"] and b["x0"] - 0.25 < c[0] < b["x1"] + 0.25 and abs(n[0]) > 0.6:
                    weight += a
                    lean += a * (c[0] - cx)
        if weight < 0.02:
            continue  # no backrest found: not a seat (a shelf, a table)
        if long_x:
            heading = 180.0 if lean > 0 else 0.0     # the back at +z: facing -z
            span, at = b["x1"] - b["x0"], [b["x0"], b["x1"]]
        else:
            heading = 270.0 if lean > 0 else 90.0    # the back at +x: facing -x
            span, at = b["z1"] - b["z0"], [b["z0"], b["z1"]]
        places = max(1, int(round(span / PLACE)))
        for k in range(places):
            along = at[0] + span * (k + 0.5) / places
            x, z = (along, cz) if long_x else (cx, along)
            if not (aisle[2] - 0.5 <= z <= aisle[3] + 0.5):
                continue
            seats.append([round(x, 3), round(top, 3), round(z, 3), heading])
    seats.sort(key=lambda s: (s[2], s[0]))

    # Partitions across the car blocking the aisle: walls (upright, facing
    # along the car) over the aisle between knee and head height.
    walls = collections.defaultdict(float)
    for t, part, n, a, c in faces:
        if abs(n[2]) > 0.9 and abs(c[0] - aisle_x) < aisle_width / 2 and floor + 0.3 < c[1] < floor + 1.8:
            walls[round(c[2] / 0.1)] += a
    passages = []
    for key in sorted(walls):
        if walls[key] < 0.25 or not (aisle[2] < key * 0.1 < aisle[3]):
            continue
        if passages and key * 0.1 - passages[-1][2] < 0.25:
            continue
        passages.append([round(aisle_x, 3), round(floor, 3), round(key * 0.1, 3), round(min(aisle_width, 0.8), 2)])
    return {"floor": floor, "aisle": aisle, "aisleWidth": round(aisle_width, 2), "benches": len(benches),
            "seats": seats, "passages": passages}


# --- Checking ----------------------------------------------------------------
def plan(tris, found, out_svg, title):
    """A top view (SVG): along the car left to right (z), across up (x);
    cushions grey, backrests dark, the aisle cyan, passages yellow, seats
    green with a tick the way the sitter faces and their numbers (a seat's
    5th value, else its place in the list from 1: the numbers write gives)."""
    scale = 60.0
    zs = [p[2] for t, _ in tris for p in t] or [-1.0, 1.0]
    z0, z1 = percentile(zs, 0.01) - 0.5, percentile(zs, 0.99) + 0.5
    width, height = (z1 - z0) * scale, 4.0 * scale

    def X(z):
        return (z - z0) * scale

    def Y(x):
        return height / 2 - x * scale
    floor = found.get("floor", 0.0)
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d">' % (width, height + 20),
             '<rect width="100%%" height="100%%" fill="white"/><text x="4" y="%d" font-size="13">%s</text>'
             % (height + 15, title.replace("&", "&amp;").replace("<", "&lt;"))]
    for t, _ in tris:
        n, a = normal_area(t)
        c = [sum(p[i] for p in t) / 3 for i in range(3)]
        if n[1] > 0.85 and CUSHION[0] < c[1] - floor < CUSHION[1]:
            parts.append('<polygon points="%s" fill="#bbb"/>' % " ".join("%.1f,%.1f" % (X(p[2]), Y(p[0])) for p in t))
        elif abs(n[1]) < 0.3 and floor + 0.6 < c[1] < floor + 1.2 and a > 0.001:
            parts.append('<polygon points="%s" fill="none" stroke="#555" stroke-width="0.5"/>'
                         % " ".join("%.1f,%.1f" % (X(p[2]), Y(p[0])) for p in t))
    for a in found.get("aisles") or ([found["aisle"]] if "aisle" in found else []):
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#0bd" stroke-width="2"/>'
                     % (X(a[2]), Y(a[0]), X(a[3]), Y(a[0])))
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
    parts.append("</svg>")
    open(out_svg, "w").write("\n".join(parts))


def plan_name(model, base=None):
    """A plan's file name: the model's path under base, '/' as '__'."""
    rel = os.path.relpath(model, base) if base else os.path.basename(model)
    return os.path.splitext(rel.replace(os.sep, "__"))[0] + ".svg"


# --- Writing -----------------------------------------------------------------
def block(found):
    lines = ["\tESD_TSRESeats ("]
    lines.append("\t\tAisle ( %s )" % " ".join(fmt(v) for v in found["aisle"]))
    for p in found["passages"]:
        lines.append("\t\tPassage ( %s )" % " ".join(fmt(v) for v in p))
    for i, s in enumerate(found["seats"]):
        lines.append("\t\tSeat %d ( %s )" % (i + 1, " ".join(fmt(v) for v in s[:4])))
    lines.append("\t)")
    return lines


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


def sd_seats(model):
    """A model's .sd block as written: {"aisles", "aisle", "passages",
    "seats" ([x, y, z, heading, number]), "floor"}; None without one."""
    sd = sd_path(model)
    if not os.path.exists(sd):
        return None
    text = read_sd(sd)
    span = block_span(text, "ESD_TSRESeats")
    if span is None:
        return None
    inner = text[span[0]:span[1]]
    inner = inner[inner.index("(") + 1:]
    out = {"aisles": [], "passages": [], "seats": []}
    for name, label, body in re.findall(r"(\w+)\s*([^\s()]*)\s*\(([^()]*)\)", inner):
        try:
            values = [float(v) for v in body.split()]
        except ValueError:
            continue
        if name.lower() == "seat" and len(values) >= 4:
            number = int(label) if label.isdigit() else len(out["seats"]) + 1
            out["seats"].append(values[:4] + [number])
        elif name.lower() == "aisle" and len(values) >= 4:
            out["aisles"].append(values[:4])
        elif name.lower() == "passage" and len(values) >= 4:
            out["passages"].append(values[:4])
    if out["aisles"]:
        out["aisle"] = out["aisles"][0]
        out["floor"] = out["aisles"][0][1]
    return out


def sd_text(model_path, found):
    sd = sd_path(model_path)
    new = block(found)
    if os.path.exists(sd):
        text = read_sd(sd).replace("\r\n", "\n")
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
    with open(sd, "wb") as f:
        f.write(b"\xff\xfe" + text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-16-le"))


def archived(archive, root, files, readme=None, backup=False):
    """<archive>.zip (or -backup.zip) of the files that exist, by their
    paths under root."""
    root = os.path.abspath(root)
    name = archive + ("-backup.zip" if backup else ".zip")
    with zipfile.ZipFile(name, "w", zipfile.ZIP_DEFLATED) as z:
        if readme:
            z.write(readme, "README.md")
        for f in files:
            if os.path.exists(f):
                z.write(f, os.path.relpath(os.path.abspath(f), root))
    return name


def write(models, root, archive, tsre, min_seats, readme=None, plans_dir=None):
    plans, skipped = [], []
    for model in models:
        if CAB.search(os.path.basename(model)):
            skipped.append((model, "a cab's model"))
            continue
        try:
            tris = triangles(model, tsre)
            found = find(tris)
        except Exception as e:  # a model the readers cannot take
            found = {"problem": repr(e)[:120]}
        if "problem" in found or len(found["seats"]) < min_seats:
            skipped.append((model, found.get("problem", "%d seats" % len(found.get("seats", [])))))
            continue
        plans.append((model,) + sd_text(model, found) + (found, tris))
    relative = lambda p: os.path.relpath(os.path.abspath(p), os.path.abspath(root))  # noqa: E731
    archived(archive, root, [sd for _, sd, _, _, _ in plans], backup=True)
    for model, sd, text, found, tris in plans:
        write_sd(sd, text)
    archived(archive, root, [sd for _, sd, _, _, _ in plans], readme)
    if plans_dir:
        os.makedirs(plans_dir, exist_ok=True)
    for model, sd, text, found, tris in plans:
        print("WROTE %s: %d seats, %d passages, aisle %s" % (relative(sd), len(found["seats"]), len(found["passages"]),
                                                             found["aisle"]))
        if plans_dir:
            plan(tris, sd_seats(model), os.path.join(plans_dir, plan_name(model, root)),
                 "%s: %d seats (its .sd)" % (relative(model), len(found["seats"])))
    for model, why in skipped:
        print("SKIPPED %s: %s" % (relative(model), why))
    print("%d written, %d skipped; %s.zip, %s-backup.zip" % (len(plans), len(skipped), archive, archive))


def drop(model, numbers, root, archive):
    """Seats taken out of a model's .sd by their numbers (the plans'); the
    others keep theirs. The .sd before in <archive>-backup.zip, after in
    <archive>.zip."""
    sd = sd_path(model)
    raw = open(sd, "rb").read()
    utf16 = raw[:2] == b"\xff\xfe"
    text = read_sd(sd)
    span = block_span(text, "ESD_TSRESeats")
    if span is None:
        sys.exit("%s has no ESD_TSRESeats" % sd)
    inner = text[span[0]:span[1]]
    wanted = set(numbers)
    gone = set()

    def out(m):
        if int(m.group(1)) in wanted:
            gone.add(int(m.group(1)))
            return ""
        return m.group(0)
    inner = re.sub(r"^[ \t]*Seat[ \t]+(\d+)[ \t]*\([^()]*\)[ \t]*\r?\n", out, inner, flags=re.M | re.I)
    archived(archive, root, [sd], backup=True)
    text = text[:span[0]] + inner + text[span[1]:]
    with open(sd, "wb") as f:
        f.write(b"\xff\xfe" + text.encode("utf-16-le") if utf16 else text.encode("utf-8"))
    archived(archive, root, [sd])
    left = len(sd_seats(model)["seats"])
    print("%s: seats %s taken out, %d left%s" % (sd, ",".join(str(n) for n in sorted(gone)), left,
          "; not found: %s" % ",".join(str(n) for n in sorted(wanted - gone)) if wanted - gone else ""))


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
    if "problem" in found:
        row["problem"] = found["problem"]
    else:
        row.update({"floor": found["floor"], "aisle": found["aisle"], "aisleWidth": found["aisleWidth"],
                    "benches": found["benches"], "seats": len(found["seats"]), "passages": len(found["passages"]),
                    "cushion": round(found["seats"][0][1] - found["floor"], 2) if found["seats"] else None,
                    "found": found})
    if "floor" in found and plans_dir:
        row["plan"] = plan_name(model, base)
        plan(tris, found, os.path.join(plans_dir, row["plan"]),
             "%s: %s" % (os.path.relpath(model, base), found.get("problem") or "%d seats" % len(found["seats"])))
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
             "- None: %d models" % len(failed)]
    if missing:
        lines.append("- Models named but not found: %d (%s)" % (sum(missing.values()), ", ".join(
            "%s x%d" % (n, c) for n, c in missing.most_common(5))))
    lines += ["", "## Seats found", "",
              "Check each plan before writing: numbers are the seats' as `write` numbers them.", "",
              "| Model | Vehicles | Kind | Seats | Benches | Cushion | Aisle | Passages | Plan |",
              "|---|---|---|---|---|---|---|---|---|"]
    for r in seated + few:
        lines.append("| `%s` | %d | %s | %d | %d | %.2f m | %.2f m | %d | [plan](plans/%s) |" % (
            rel(r["model"]), r["used"], ", ".join(r["kinds"]), r["seats"], r["benches"], r["cushion"] or 0.0,
            r["aisleWidth"], r["passages"], r.get("plan", "")))
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
    ap.add_argument("command", choices=["scan", "find", "plan", "write", "drop"])
    ap.add_argument("paths", nargs="+", help="models; scan: vehicle folders; drop: a model, then seat numbers")
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
    a = ap.parse_args()
    if a.command == "scan":
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
            out = os.path.join(a.out or ".", plan_name(model))
            plan(tris, found, out, "%s: %s" % (os.path.basename(model), found.get("problem") or "%d seats%s" % (
                len(found["seats"]), " (its .sd)" if a.sd else "")))
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
        drop(a.paths[0], [int(n) for n in a.paths[1:]], a.root, a.archive)
    else:
        if not a.root or not a.archive:
            ap.error("write needs --root and --archive")
        write(a.paths, a.root, a.archive, a.tsre, a.min_seats, a.readme, a.plans)


if __name__ == "__main__":
    main()
