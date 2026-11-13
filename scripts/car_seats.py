#!/usr/bin/env python3
"""Sitting places of cars, for their .sd (task humans 09): ESD_TSRESeats with
the car's Aisle, its Passages (partitions' closed doors across the aisle) and
its Seats, found in the model's most detailed level (an MSTS .s through
TSRE's converter, a MaSzyna .e3d by its records), in the model's space as
written (an S file's as its file's, as the lights' positions).

  find:  python3 -I scripts/car_seats.py find <model> ... [--tsre build/TSRE5vc] [--json out.json]
  write: python3 -I scripts/car_seats.py write <model> ... --root <game root> --archive <path prefix>
         [--tsre ...] [--min-seats 8]

How a car is read: the floor is the height with the most upward area inside
the body; cushions are upward faces 0.33 to 0.58 m above it, grouped into
benches (one side of the aisle, next to each other along the car); a
bench's back is the side with its backrest (upright faces above the
cushion), the sitter faces away from it; its places are 0.45 m apart along
the bench. The aisle runs between the benches' inner edges, along the floor
around the car's middle; a partition is a wall across the car blocking the
aisle (its door modelled closed): a Passage.

write: each model's .sd beside it, its other lines kept and an
ESD_TSRESeats block put in (a new .sd: the route's header, ESD_Detail_Level
and ESD_Alternative_Texture 0); UTF-16 with BOM and CRLF, as the route's.
Before writing, <archive>-backup.zip holds the .sd files there were; after,
<archive>.zip the written ones; both by their paths under --root, to
extract into it. Cars with fewer seats than --min-seats are left out.
"""
import argparse, collections, json, math, os, re, struct, subprocess, sys, tempfile, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lamp_shapes import tree, find as find_block, first, blocks, mul, to_text  # noqa: E402

PLACE = 0.45          # a place's width on a bench (m)
CUSHION = (0.33, 0.58)  # cushion tops above the floor (m)
HEADER = "SIMISA@@@@@@@@@@JINX0t1t______"
# Parts of a cab (MSTS matrices, MaSzyna parts): no passengers' seats there.
CAB = re.compile(r"cab|kabin|maszynist|mechanik|driver|fuehrer|f\u00fchrer", re.I)


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
    floors = collections.Counter()
    for t, part, n, a, c in faces:
        if n[1] > 0.95 and 0.6 < c[1] < 1.8 and all(inside(p, 0.12) for p in t):
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
    green with a tick the way the sitter faces."""
    scale = 60.0
    zs = [p[2] for t, _ in tris for p in t]
    z0, z1 = percentile(zs, 0.01) - 0.5, percentile(zs, 0.99) + 0.5
    width, height = (z1 - z0) * scale, 4.0 * scale

    def X(z):
        return (z - z0) * scale

    def Y(x):
        return height / 2 - x * scale
    floor = found.get("floor", 0.0)
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d">' % (width, height + 20),
             '<rect width="100%%" height="100%%" fill="white"/><text x="4" y="%d" font-size="13">%s</text>'
             % (height + 15, title)]
    for t, _ in tris:
        n, a = normal_area(t)
        c = [sum(p[i] for p in t) / 3 for i in range(3)]
        if n[1] > 0.85 and CUSHION[0] < c[1] - floor < CUSHION[1]:
            parts.append('<polygon points="%s" fill="#bbb"/>' % " ".join("%.1f,%.1f" % (X(p[2]), Y(p[0])) for p in t))
        elif abs(n[1]) < 0.3 and floor + 0.6 < c[1] < floor + 1.2 and a > 0.001:
            parts.append('<polygon points="%s" fill="none" stroke="#555" stroke-width="0.5"/>'
                         % " ".join("%.1f,%.1f" % (X(p[2]), Y(p[0])) for p in t))
    if "aisle" in found:
        a = found["aisle"]
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#0bd" stroke-width="2"/>'
                     % (X(a[2]), Y(a[0]), X(a[3]), Y(a[0])))
    for p in found.get("passages", []):
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#eb0" stroke-width="3"/>'
                     % (X(p[2]), Y(p[0] - p[3] / 2), X(p[2]), Y(p[0] + p[3] / 2)))
    for s in found.get("seats", []):
        h = math.radians(s[3])
        parts.append('<circle cx="%.1f" cy="%.1f" r="4" fill="#1a1"/>' % (X(s[2]), Y(s[0])))
        parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#1a1" stroke-width="2"/>'
                     % (X(s[2]), Y(s[0]), X(s[2] + 0.3 * math.cos(h)), Y(s[0] + 0.3 * math.sin(h))))
    parts.append("</svg>")
    open(out_svg, "w").write("\n".join(parts))


# --- Writing -----------------------------------------------------------------
def block(found):
    lines = ["\tESD_TSRESeats ("]
    lines.append("\t\tAisle ( %s )" % " ".join(fmt(v) for v in found["aisle"]))
    for p in found["passages"]:
        lines.append("\t\tPassage ( %s )" % " ".join(fmt(v) for v in p))
    for s in found["seats"]:
        lines.append("\t\tSeat ( %s )" % " ".join(fmt(v) for v in s))
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


def remove_block(text, name):
    """The text without its `name ( ... )` block (balanced brackets)."""
    m = re.search(r"^[ \t]*" + name + r"\s*\(", text, re.M | re.I)
    if not m:
        return text
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
    return text[:m.start()] + text[end + 1 if end >= 0 else len(text):]


def sd_text(model_path, found):
    sd = os.path.splitext(model_path)[0] + ".sd"
    new = block(found)
    if os.path.exists(sd):
        text = read_sd(sd).replace("\r\n", "\n")
        text = remove_block(text, "ESD_TSRESeats")
        close = text.rstrip().rfind(")")
        text = text[:close].rstrip("\n") + "\n" + "\n".join(new) + "\n" + text[close:]
    else:
        text = "\n".join([HEADER, "", "Shape ( %s" % os.path.basename(model_path), "\tESD_Detail_Level ( 0 )",
                          "\tESD_Alternative_Texture ( 0 )"] + new + [")", ""])
    return sd, text


def write(models, root, archive, tsre, min_seats):
    plans, skipped = [], []
    for model in models:
        try:
            found = find(triangles(model, tsre))
        except Exception as e:  # a model the readers cannot take
            found = {"problem": repr(e)[:120]}
        if "problem" in found or len(found["seats"]) < min_seats:
            skipped.append((model, found.get("problem", "%d seats" % len(found.get("seats", [])))))
            continue
        plans.append((model,) + sd_text(model, found) + (found,))
    root = os.path.abspath(root)
    relative = lambda p: os.path.relpath(os.path.abspath(p), root)  # noqa: E731
    with zipfile.ZipFile(archive + "-backup.zip", "w", zipfile.ZIP_DEFLATED) as z:
        for model, sd, text, found in plans:
            if os.path.exists(sd):
                z.write(sd, relative(sd))
    for model, sd, text, found in plans:
        with open(sd, "wb") as f:
            f.write(b"\xff\xfe" + text.replace("\n", "\r\n").encode("utf-16-le"))
    with zipfile.ZipFile(archive + ".zip", "w", zipfile.ZIP_DEFLATED) as z:
        for model, sd, text, found in plans:
            z.write(sd, relative(sd))
    for model, sd, text, found in plans:
        print("WROTE %s: %d seats, %d passages, aisle %s" % (relative(sd), len(found["seats"]), len(found["passages"]),
                                                             found["aisle"]))
    for model, why in skipped:
        print("SKIPPED %s: %s" % (relative(model), why))
    print("%d written, %d skipped; %s.zip, %s-backup.zip" % (len(plans), len(skipped), archive, archive))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["find", "write", "plan"])
    ap.add_argument("models", nargs="+")
    ap.add_argument("--tsre", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "TSRE5vc"))
    ap.add_argument("--json")
    ap.add_argument("--root")
    ap.add_argument("--archive")
    ap.add_argument("--min-seats", type=int, default=8)
    a = ap.parse_args()
    if a.command == "plan":
        # plan: one SVG top view per model, beside --archive's prefix (a folder).
        os.makedirs(a.archive or ".", exist_ok=True)
        for model in a.models:
            tris = triangles(model, a.tsre)
            found = find(tris)
            out = os.path.join(a.archive or ".", os.path.splitext(os.path.basename(model))[0] + ".svg")
            plan(tris, found, out, "%s: %s" % (os.path.basename(model), found.get("problem") or
                                               "%d seats" % len(found["seats"])))
            print(out)
        return
    if a.command == "find":
        out = []
        for model in a.models:
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
    else:
        if not a.root or not a.archive:
            ap.error("write needs --root and --archive")
        write(a.models, a.root, a.archive, a.tsre, a.min_seats)


if __name__ == "__main__":
    main()
