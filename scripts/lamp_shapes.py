#!/usr/bin/env python3
"""Lamp shapes of an MSTS route, for adding .sd lights (task assets 05).

scan:  every shape the route's W files place, with its placements, kept when
       a lamp word is in its file name or inside it (texture, matrix names);
       with --tsre each kept shape is converted to text and its full-bright
       parts and lamp-textured parts are listed (where its glass is).
heads: one shape's lamp heads: its full-bright triangles (vertex state light
       material -8) of the most detailed level, in the root matrix's space
       (an .sd light's Position), clustered; and each prim state's box.

  python3 -I scripts/lamp_shapes.py scan <route dir> <out prefix> [--tsre build/TSRE5vc]
  python3 -I scripts/lamp_shapes.py heads <shape .s> [--tsre build/TSRE5vc]

Only W files named w±XXXXXX±XXXXXX.w count (TSRE loads no others). Nothing
is written into the route: converted shapes go to <out prefix>-text/ or a
temporary folder.
"""
import argparse, collections, glob, json, os, re, subprocess, sys, tempfile, zlib

WORDS = ["lamp", "latarn", "latern", "leucht", "licht", "light", "swiatl", "oswietl",
         "osw_", "flood", "reflektor", "kandel", "lantern", "beleucht", "strassenl"]
WORD_RE = re.compile("|".join(WORDS), re.I)
# Block names every text shape has.
NOISE = {"light_materials", "light_model_cfg", "light_model_cfgs", "subobject_light_cfgs", "light_material"}
LAMP_TEXTURE = re.compile(r"lamp|latarn|latern|swiatl|licht|light|leucht|oprawa", re.I)


# --- W files -----------------------------------------------------------------

def placements(route):
    block_re = re.compile(r"^\t(\w+) \($", re.M)
    file_re = re.compile(r'^\t\tFileName \( "?([^")]+?)"? \)$', re.M)
    pos_re = re.compile(r"^\t\tPosition \( (\S+) (\S+) (\S+) \)$", re.M)
    uid_re = re.compile(r"^\t\tUiD \( (\d+) \)$", re.M)
    placed = collections.defaultdict(lambda: {"count": 0, "kinds": collections.Counter(), "where": []})
    for path in glob.glob(os.path.join(route, "WORLD", "*.w")):
        m = re.fullmatch(r"w([+-]\d{6})([+-]\d{6})\.w", os.path.basename(path))
        if not m:
            continue
        tx, tz = int(m.group(1)), int(m.group(2))
        text = open(path, "rb").read().decode("utf-16", "replace").replace("\r\n", "\n")
        blocks = list(block_re.finditer(text))
        for i, b in enumerate(blocks):
            body = text[b.start():blocks[i + 1].start() if i + 1 < len(blocks) else len(text)]
            f = file_re.search(body)
            if not f or not f.group(1).lower().endswith(".s"):
                continue
            rec = placed[f.group(1)]
            rec["count"] += 1
            rec["kinds"][b.group(1)] += 1
            p, u = pos_re.search(body), uid_re.search(body)
            if p:
                rec["where"].append([tx, tz, float(p.group(1)), float(p.group(2)), float(p.group(3)),
                                     int(u.group(1)) if u else -1])
    return placed


# --- Shapes ------------------------------------------------------------------

def raw_text(path):
    """A shape's strings, readable whatever its format."""
    raw = open(path, "rb").read()
    if raw[:2] == b"\xff\xfe":
        return raw[2:].decode("utf-16-le", "replace")
    if raw[:8] == b"SIMISA@F":
        try:
            raw = zlib.decompress(raw[16:])
        except zlib.error:
            return ""
    return raw.decode("utf-16-le", "replace") + raw[1:].decode("utf-16-le", "replace")


def to_text(tsre, shape, out):
    subprocess.run([tsre, "--shapeconv", "-i", shape, "-o", out, "-f", "unicode", "-c", "uncompressed", "-w"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def tree(text):
    root, stack, cur = [], [], None
    cur = root
    for tok in re.findall(r'\(|\)|"[^"]*"|[^\s()]+', text):
        if tok == "(":
            new = []
            cur.append(new)
            stack.append(cur)
            cur = new
        elif tok == ")":
            cur = stack.pop()
        else:
            cur.append(tok)
    return root


def blocks(lst, name):
    """(label, block) for each `name [label] ( … )` directly in lst."""
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


def find(lst, name):
    b = first(lst, name)
    if b is not None:
        return b
    for item in lst:
        if isinstance(item, list):
            r = find(item, name)
            if r is not None:
                return r
    return None


def mul(p, m):
    # MSTS row vector: p * R + t; m = R's three rows, then t.
    return [p[0] * m[0] + p[1] * m[3] + p[2] * m[6] + m[9],
            p[0] * m[1] + p[1] * m[4] + p[2] * m[7] + m[10],
            p[0] * m[2] + p[1] * m[5] + p[2] * m[8] + m[11]]


class NightTextures:
    """Night textures by name, sampled for brightness (0-255): converted to
    PPM by TSRE's --aceconv into a cache folder."""

    def __init__(self, night_dir, tsre, cache):
        self.dir, self.tsre, self.cache, self.loaded = night_dir, tsre, cache, {}
        self.names = {n.lower(): n for n in os.listdir(night_dir)}
        os.makedirs(cache, exist_ok=True)

    def __call__(self, name):
        key = name.lower()
        if key not in self.loaded:
            self.loaded[key] = None
            found = self.names.get(key)
            if found:
                ppm = os.path.join(self.cache, found + ".ppm")
                if not os.path.exists(ppm):
                    subprocess.run([self.tsre, "--aceconv", "-i", os.path.join(self.dir, found), "-o", ppm, "-w"],
                                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                data = open(ppm, "rb").read()
                head = re.match(rb"P6\s+(?:#[^\n]*\s+)*(\d+)\s+(\d+)\s+(\d+)\s", data)
                w, h = int(head.group(1)), int(head.group(2))
                self.loaded[key] = (w, h, data[head.end():])
        return self.loaded[key]


def texel(image, uv):
    w, h, px = image
    x, y = int((uv[0] % 1.0) * w) % w, int((uv[1] % 1.0) * h) % h
    i = (y * w + x) * 3
    return px[i:i + 3]


def brightness(image, uv):
    return sum(texel(image, uv)) / 3


def heads(text_path, radius=0.7, texture=None, any_lighting=False, night=None, glow=150):
    """texture: a regular expression; only the triangles drawn with a
    matching texture are clustered. any_lighting: not only full-bright ones
    (a head without full-bright glass). night: a NightTextures; then the
    triangles whose night texture is bright (above glow, at its corners and
    centre) are clustered instead: the glass a night texture paints lit."""
    texture_re = re.compile(texture, re.I) if texture else None
    shape = find(tree(open(text_path, "rb").read().decode("utf-16")), "shape")
    points = [[float(v) for v in b] for _, b in blocks(first(shape, "points"), "point")]
    uvs = [[float(v) for v in b] for _, b in blocks(first(shape, "uv_points") or [], "uv_point")]
    matrices = blocks(first(shape, "matrices"), "matrix")
    names = [label for label, _ in matrices]
    mats = [[float(v) for v in b] for _, b in matrices]
    images = [b[0].strip('"') for _, b in blocks(first(shape, "images"), "image")]
    textures = [int(b[0]) for _, b in blocks(first(shape, "textures"), "texture")]
    vtx_states = [[int(v) for v in b[:5]] for _, b in blocks(first(shape, "vtx_states"), "vtx_state")]
    prim_states = []
    for label, b in blocks(first(shape, "prim_states"), "prim_state"):
        tex = first(b, "tex_idxs")
        rest = [x for x in b[b.index(tex) + 1:] if isinstance(x, str)]
        prim_states.append({"name": label, "tex": [images[textures[int(i)]] for i in tex[1:]], "vtx": int(rest[1])})
    levels = blocks(find(shape, "distance_levels"), "distance_level")
    level = levels[0][1]
    header = first(level, "distance_level_header")
    hierarchy = [int(v) for v in first(header, "hierarchy")[1:]]

    def to_root(p, m):
        while m > 0:              # up to, not through, the root matrix
            p = mul(p, mats[m])
            m = hierarchy[m]
        return p

    bright, states = [], {}
    for _, sub in blocks(first(level, "sub_objects"), "sub_object"):
        verts, vuv = [], []
        for _, b in blocks(first(sub, "vertices"), "vertex"):
            verts.append(int(b[1]))
            u = first(b, "vertex_uvs")
            vuv.append(uvs[int(u[1])] if u and int(u[0]) > 0 and uvs else None)
        prims = first(sub, "primitives")
        state = None
        for i, item in enumerate(prims):
            if item == "prim_state_idx":
                state = int(prims[i + 1][0])
            elif item == "indexed_trilist":
                ps = prim_states[state]
                vs = vtx_states[ps["vtx"]]
                idx = [int(v) for v in first(prims[i + 1], "vertex_idxs")[1:]]
                tris = [[to_root(points[verts[v]], vs[1]) for v in idx[k:k + 3]] for k in range(0, len(idx), 3)]
                box = states.setdefault((ps["name"], tuple(ps["tex"]), names[vs[1]], vs[2]), [[1e9] * 3, [-1e9] * 3, 0])
                for tri in tris:
                    for q in tri:
                        for a in range(3):
                            box[0][a], box[1][a] = min(box[0][a], q[a]), max(box[1][a], q[a])
                    box[2] += 1
                if night:
                    image = night(ps["tex"][0]) if ps["tex"] else None
                    if image is None or (texture_re and not texture_re.search(" ".join(ps["tex"]))):
                        continue
                    for k in range(0, len(idx), 3):
                        corners = [vuv[v] for v in idx[k:k + 3]]
                        if None in corners:
                            continue
                        centre = [sum(c[a] for c in corners) / 3 for a in range(2)]
                        if min(brightness(image, c) for c in corners + [centre]) > glow:
                            bright.append((tris[k // 3], ps["tex"], names[vs[1]], list(texel(image, centre))))
                    continue
                for tri in tris:
                    if (any_lighting or vs[2] == -8) and (not texture_re or texture_re.search(" ".join(ps["tex"]))):
                        bright.append((tri, ps["tex"], names[vs[1]], None))
    clusters = []
    for tri, tex, matrix, rgb in bright:
        c = [sum(p[a] for p in tri) / 3 for a in range(3)]
        for cl in clusters:
            if cl["matrix"] == matrix and all(abs(c[a] - cl["c"][a]) < radius for a in range(3)):
                cl["pts"] += tri
                cl["tex"].update(tex)
                cl["n"] += 1
                cl["c"] = [(cl["c"][a] * (cl["n"] - 1) + c[a]) / cl["n"] for a in range(3)]
                if rgb:
                    cl["rgb"].append(rgb)
                break
        else:
            clusters.append({"c": c, "pts": list(tri), "tex": set(tex), "n": 1, "matrix": matrix,
                             "rgb": [rgb] if rgb else []})

    def colour(samples):
        # The night colour, scaled to full brightness (hex), for a light's ColorRGBA.
        if not samples:
            return None
        avg = [sum(s[a] for s in samples) / len(samples) for a in range(3)]
        top = max(max(avg), 1)
        return "".join("%02x" % round(255 * v / top) for v in avg)
    r2 = lambda v: [round(x, 2) for x in v]
    return {
        "matrices": names, "levels": len(levels),
        "dlevel": float(first(header, "dlevel_selection")[0]),
        "heads": [{"center": r2([(min(p[a] for p in cl["pts"]) + max(p[a] for p in cl["pts"])) / 2 for a in range(3)]),
                   "min": r2([min(p[a] for p in cl["pts"]) for a in range(3)]),
                   "max": r2([max(p[a] for p in cl["pts"]) for a in range(3)]),
                   "tris": cl["n"], "tex": sorted(cl["tex"]), "matrix": cl["matrix"],
                   "night_colour": colour(cl["rgb"])} for cl in clusters],
        "states": [{"state": k[0], "tex": list(k[1]), "matrix": k[2], "light": k[3],
                    "min": r2(b[0]), "max": r2(b[1]), "tris": b[2]} for k, b in states.items()],
    }


# --- Commands ----------------------------------------------------------------

def scan(route, out, tsre):
    shapes = os.path.join(route, "SHAPES")
    rows = []
    for name, rec in placements(route).items():
        if "/" in name or "\\" in name:
            continue                               # track shapes in GLOBAL
        path = os.path.join(shapes, name)
        exists = os.path.exists(path)
        inner = sorted({w.lower() for w in re.findall(r"[\w\-.]*(?:" + "|".join(WORDS) + r")[\w\-.]*",
                                                         raw_text(path), re.I)} - NOISE) if exists else []
        by_name = bool(WORD_RE.search(name))
        if not (by_name or inner):
            continue
        sd = os.path.splitext(path)[0] + ".sd"
        night, lights = None, 0
        if os.path.exists(sd):
            t = open(sd, "rb").read().decode("utf-16", "replace")
            a = re.search(r"ESD_Alternative_Texture \( (\d+) \)", t, re.I)
            night = bool(a and int(a.group(1)) & 256)
            lights = len(re.findall(r"ESD_TSRELight", t, re.I))
        rows.append({"shape": name, "count": rec["count"], "kinds": dict(rec["kinds"]), "exists": exists,
                     "by_name": by_name, "inner": inner[:6], "night": night, "lights": lights,
                     "where": rec["where"]})
    rows.sort(key=lambda r: (-r["count"], r["shape"].lower()))
    if tsre:
        text_dir = out + "-text"
        os.makedirs(text_dir, exist_ok=True)
        for r in rows:
            if not r["exists"]:
                continue
            text = os.path.join(text_dir, r["shape"])
            try:
                if not os.path.exists(text):
                    to_text(tsre, os.path.join(shapes, r["shape"]), text)
                h = heads(text)
            except Exception as e:                  # noqa: BLE001 - report and go on
                r["error"] = str(e)
                continue
            fmt = lambda s: "%s@y%.1f-%.1f" % (",".join(t.rsplit(".", 1)[0] for t in s["tex"]), s["min"][1], s["max"][1])
            r["bright"] = [fmt(s) for s in h["states"] if s["light"] == -8]
            r["lamp_states"] = [fmt(s) for s in h["states"] if any(LAMP_TEXTURE.search(t) for t in s["tex"])]
            r["levels"] = h["levels"]
    json.dump(rows, open(out + ".json", "w"), indent=1)
    with open(out + ".txt", "w") as f:
        for r in rows:
            kinds = ",".join(f"{k}:{v}" for k, v in r["kinds"].items())
            f.write(f"{r['count']:5d} {r['shape']:40s} name={'Y' if r['by_name'] else '-'} night={str(r['night'])[0]}"
                    f" lights={r['lights']} {kinds:18s} bright[{'; '.join(r.get('bright', []))}]"
                    f" lamp[{'; '.join(r.get('lamp_states', []))}] inner={' '.join(r['inner'])}\n")
    print(len(rows), "candidates ->", out + ".json,", out + ".txt")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    s = sub.add_parser("scan")
    s.add_argument("route")
    s.add_argument("out")
    s.add_argument("--tsre")
    h = sub.add_parser("heads")
    h.add_argument("shape")
    h.add_argument("--tsre")
    h.add_argument("--radius", type=float, default=0.7)
    h.add_argument("--texture", help="cluster only the parts drawn with a texture matching this")
    h.add_argument("--any-lighting", action="store_true", help="also parts that are not full-bright")
    h.add_argument("--night-dir", help="the route's TEXTURES/NIGHT: cluster the parts its night textures paint lit")
    h.add_argument("--glow", type=float, default=150, help="night brightness (0-255) that counts as lit")
    a = ap.parse_args()
    if a.command == "scan":
        scan(a.route, a.out, a.tsre)
        return
    path = a.shape
    if a.tsre:
        tmp = tempfile.mkdtemp()
        path = os.path.join(tmp, os.path.basename(a.shape))
        to_text(a.tsre, a.shape, path)
    night = None
    if a.night_dir:
        if not a.tsre:
            sys.exit("--night-dir needs --tsre")
        night = NightTextures(a.night_dir, a.tsre, os.path.join(tempfile.gettempdir(), "lamp_shapes_night"))
    json.dump(heads(path, a.radius, a.texture, a.any_lighting, night, a.glow), sys.stdout, indent=1)
    print()


if __name__ == "__main__":
    main()
