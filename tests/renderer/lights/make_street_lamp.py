# A street lamp: 6 m pole, 1.2 m arm, a head with a warm emissive lens and a
# downward spot light (KHR_lights_punctual). Metres, y up, the arm along +x.
import json, struct, base64, math, sys
out = sys.argv[1]
parts = []  # (positions, normals, indices, material)

def box(cx, cy, cz, sx, sy, sz, material):
    pos, nor, idx = [], [], []
    faces = [((1,0,0),(0,1,0),(0,0,1)), ((-1,0,0),(0,1,0),(0,0,-1)),
             ((0,1,0),(0,0,1),(1,0,0)), ((0,-1,0),(0,0,-1),(1,0,0)),
             ((0,0,1),(1,0,0),(0,1,0)), ((0,0,-1),(-1,0,0),(0,1,0))]
    h = (sx/2, sy/2, sz/2)
    for n, u, v in faces:
        base = len(pos)//3
        for a, b in ((-1,-1),(1,-1),(1,1),(-1,1)):
            p = [ [cx,cy,cz][k] + (n[k] + a*u[k] + b*v[k]) * h[k] for k in range(3)]
            pos += p; nor += list(n)
        idx += [base, base+1, base+2, base, base+2, base+3]
    parts.append((pos, nor, idx, material))

def cylinder(cx, cz, y0, y1, r, material, segments=16):
    pos, nor, idx = [], [], []
    for i in range(segments+1):
        a = 2*math.pi*i/segments
        x, z = math.cos(a), math.sin(a)
        pos += [cx + r*x, y0, cz + r*z, cx + r*x, y1, cz + r*z]
        nor += [x, 0, z, x, 0, z]
    for i in range(segments):
        a, b = 2*i, 2*i+2
        idx += [a, a+1, b, b, a+1, b+1]
    parts.append((pos, nor, idx, material))

POLE, LENS = 0, 1
cylinder(0, 0, 0.0, 6.0, 0.09, POLE)
box(0.6, 6.0, 0.0, 1.3, 0.08, 0.08, POLE)          # arm
box(1.25, 5.95, 0.0, 0.55, 0.14, 0.28, POLE)       # head
box(1.25, 5.87, 0.0, 0.45, 0.02, 0.2, LENS)        # lens under the head

buf = b''; views = []; accessors = []; prims = []
def add(data, fmt, target):
    global buf
    while len(buf) % 4: buf += b'\0'
    off = len(buf); blob = struct.pack('<%d%s' % (len(data), fmt), *data); buf += blob
    views.append({"buffer": 0, "byteOffset": off, "byteLength": len(blob), "target": target})
    return len(views) - 1
for pos, nor, idx, mat in parts:
    pv = add(pos, 'f', 34962); nv = add(nor, 'f', 34962); iv = add(idx, 'H', 34963)
    xs, ys, zs = pos[0::3], pos[1::3], pos[2::3]
    accessors.append({"bufferView": pv, "componentType": 5126, "count": len(pos)//3, "type": "VEC3",
                      "min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]})
    accessors.append({"bufferView": nv, "componentType": 5126, "count": len(nor)//3, "type": "VEC3"})
    accessors.append({"bufferView": iv, "componentType": 5123, "count": len(idx), "type": "SCALAR"})
    a = len(accessors) - 3
    prims.append({"attributes": {"POSITION": a, "NORMAL": a+1}, "indices": a+2, "material": mat})

gltf = {
    "asset": {"version": "2.0", "generator": "TSRE5 test street lamp"},
    "extensionsUsed": ["KHR_lights_punctual", "KHR_materials_emissive_strength"],
    "extensions": {"KHR_lights_punctual": {"lights": [
        {"name": "Street light", "type": "spot", "color": [1.0, 0.82, 0.6], "intensity": 60.0,
         "range": 30.0, "spot": {"innerConeAngle": 0.6, "outerConeAngle": 1.05}}]}},
    "materials": [
        {"name": "Painted steel", "pbrMetallicRoughness": {"baseColorFactor": [0.18, 0.2, 0.2, 1.0],
                                                          "metallicFactor": 0.6, "roughnessFactor": 0.5}},
        {"name": "Lens", "emissiveFactor": [1.0, 0.82, 0.6],
         "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 6.0}},
         "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.9, 0.85, 1.0], "metallicFactor": 0.0,
                                  "roughnessFactor": 0.3}}],
    "meshes": [{"name": "Lamp", "primitives": prims}],
    # The light hangs under the lens, pointing down (-z of the node turned to -y).
    "nodes": [{"name": "Lamp", "mesh": 0},
              {"name": "Light", "translation": [1.25, 5.84, 0.0],
               "rotation": [-math.sin(math.pi/4), 0.0, 0.0, math.cos(math.pi/4)],
               "extensions": {"KHR_lights_punctual": {"light": 0}}}],
    "scenes": [{"nodes": [0, 1]}], "scene": 0,
    "bufferViews": views, "accessors": accessors,
    "buffers": [{"byteLength": len(buf), "uri": "data:application/octet-stream;base64," + base64.b64encode(buf).decode()}]}
json.dump(gltf, open(out, "w"), indent=1)
print("written", out, len(buf), "bytes")
