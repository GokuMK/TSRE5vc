"""Test shape for MSTS materials (docs/tasks/shapes/13-msts-pbr-materials-and-editor.md):
six spheres on a ground plane, left to right: diffuse (light material -5),
sheen (-6, Specular 25), glint (-7, Specular 750), clearcoated paint
(PbrTexDiff: base colour, ORM(C) with no varnish on its white stripe, a riveted
normal map), metal (PbrTexDiff: polished with a rough band), glass
(BlendAPbrTexDiff). Writes material-spheres.s (Unicode text), its .sd with the
ESD_TSREMaterial lines and the PNG textures into the given directory.

    python scripts/make-material-spheres.py <directory>
"""
import math
import os
import struct
import sys
import zlib

SIZE = 256


def png(path, w, h, pixels, channels):
    raw = b''.join(b'\x00' + bytes(pixels[y * w * channels:(y + 1) * w * channels]) for y in range(h))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    colour = 6 if channels == 4 else 2
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, colour, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def image(path, texel, channels):
    pixels = bytearray(SIZE * SIZE * channels)
    for y in range(SIZE):
        for x in range(SIZE):
            pixels[(y * SIZE + x) * channels:(y * SIZE + x + 1) * channels] = bytes(texel(x / SIZE, y / SIZE))
    png(path, SIZE, SIZE, pixels, channels)


def stripe(v):
    return 0.45 <= v < 0.55


def rivets(u, v):
    """Height 0..1: rivets every 1/8, a groove along v = 0.25 and 0.75."""
    cu, cv = (u * 8) % 1 - 0.5, (v * 8) % 1 - 0.5
    r = math.hypot(cu, cv) / 0.18
    h = math.sqrt(max(0.0, 1 - r * r)) if r < 1 else 0.0
    for g in (0.25, 0.75):
        d = abs(v - g) * SIZE
        if d < 1.5:
            h -= 0.6 * (1 - d / 1.5)
    return h


def normal(u, v):
    e = 1.0 / SIZE
    # OpenGL: green up in the image (v grows down).
    dx = (rivets(u + e, v) - rivets(u - e, v)) * 0.5 * 6
    dy = (rivets(u, v - e) - rivets(u, v + e)) * 0.5 * 6
    n = (-dx, -dy, 1.0)
    length = math.sqrt(sum(c * c for c in n))
    return [int(round((c / length * 0.5 + 0.5) * 255)) for c in n]


def textures(directory):
    image(os.path.join(directory, 'spheres_grey.png'),
          lambda u, v: [136, 136, 136] if (int(u * 8) + int(v * 8)) % 2 else [118, 118, 118], 3)
    image(os.path.join(directory, 'spheres_paint.png'),
          lambda u, v: [235, 235, 230] if stripe(v) else [170, 24, 20], 3)
    image(os.path.join(directory, 'spheres_metal.png'), lambda u, v: [205, 205, 210], 3)
    image(os.path.join(directory, 'spheres_glass.png'), lambda u, v: [190, 210, 220, 50], 4)
    # ORM(C): occlusion, roughness, metalness, clearcoat.
    image(os.path.join(directory, 'spheres_paint_orm.png'),
          lambda u, v: [255, 150, 0, 0] if stripe(v) else [255, 110, 0, 255], 4)
    image(os.path.join(directory, 'spheres_metal_orm.png'),
          lambda u, v: [255, 210, 255, 0] if 0.4 <= v < 0.6 else [255, 50, 255, 0], 4)
    image(os.path.join(directory, 'spheres_rivets_n.png'), normal, 3)


def shape(directory):
    points, uvs, normals, vertices = [], [], [], []
    primitives = []  # (prim state, triangle vertex index list)

    def add_vertex(p, n, uv):
        points.append(p)
        normals.append(n)
        uvs.append(uv)
        vertices.append(len(points) - 1)
        return len(vertices) - 1

    def sphere(cx, state, segments=32, rings=16, radius=1.2, cy=1.4):
        first = len(vertices)
        for r in range(rings + 1):
            lat = math.pi * r / rings
            for s in range(segments + 1):
                lon = 2 * math.pi * s / segments
                n = (math.sin(lat) * math.cos(lon), math.cos(lat), math.sin(lat) * math.sin(lon))
                add_vertex((cx + radius * n[0], cy + radius * n[1], radius * n[2]), n, (s / segments, r / rings))
        tris = []
        for r in range(rings):
            for s in range(segments):
                a = first + r * (segments + 1) + s
                b, c, d = a + 1, a + segments + 1, a + segments + 2
                # Outward: cross(p1 - p0, p2 - p0) along the normal.
                tris += [a, b, d, a, d, c]
        primitives.append((state, tris))

    def ground(state):
        first = len(vertices)
        for x, z in ((-10, -3.5), (10, -3.5), (10, 3.5), (-10, 3.5)):
            add_vertex((x, 0.0, z), (0.0, 1.0, 0.0), ((x + 10) / 4, (z + 3.5) / 4))
        primitives.append((state, [first, first + 2, first + 1, first, first + 3, first + 2]))

    # Fix the winding: the ground's triangles are made outward like the spheres'.
    for i, state in enumerate([0, 1, 2, 3, 4, 5]):
        sphere(-7.5 + 3 * i, state)
    ground(6)
    for state, tris in primitives:
        for k in range(0, len(tris), 3):
            p = [points[vertices[t]] for t in tris[k:k + 3]]
            e1 = [p[1][c] - p[0][c] for c in range(3)]
            e2 = [p[2][c] - p[0][c] for c in range(3)]
            cross = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
            n = normals[vertices[tris[k]]]
            if sum(cross[c] * n[c] for c in range(3)) < 0:
                tris[k + 1], tris[k + 2] = tris[k + 2], tris[k + 1]

    f = lambda v: ('%.6f' % v).rstrip('0').rstrip('.') if v != 0 else '0'
    out = ['SIMISA@@@@@@@@@@JINX0s1t______', '', 'shape (', '\tshape_header ( 00000000 00000000 )',
           '\tvolumes ( 1', '\t\tvol_sphere (', '\t\t\tvector ( 0 1.4 0 ) 10.5', '\t\t)', '\t)',
           '\tshader_names ( 3', '\t\tnamed_shader ( TexDiff )', '\t\tnamed_shader ( PbrTexDiff )',
           '\t\tnamed_shader ( BlendAPbrTexDiff )', '\t)',
           '\ttexture_filter_names ( 1', '\t\tnamed_filter_mode ( MipLinear )', '\t)',
           '\tpoints ( %d' % len(points)]
    out += ['\t\tpoint ( %s %s %s )' % tuple(f(c) for c in p) for p in points]
    out += ['\t)', '\tuv_points ( %d' % len(uvs)]
    out += ['\t\tuv_point ( %s %s )' % (f(u), f(v)) for u, v in uvs]
    out += ['\t)', '\tnormals ( %d' % len(normals)]
    out += ['\t\tvector ( %s %s %s )' % tuple(f(c) for c in n) for n in normals]
    images = ['spheres_grey.png', 'spheres_paint.png', 'spheres_metal.png', 'spheres_glass.png',
              'spheres_paint_orm.png', 'spheres_metal_orm.png', 'spheres_rivets_n.png']
    out += ['\t)', '\tsort_vectors ( 0 )', '\tcolours ( 0 )', '\tmatrices ( 1',
            '\t\tmatrix MAIN ( 1 0 0 0 1 0 0 0 1 0 0 0 )', '\t)', '\timages ( %d' % len(images)]
    out += ['\t\timage ( %s )' % name for name in images]
    out += ['\t)', '\ttextures ( %d' % len(images)]
    out += ['\t\ttexture ( %d 0 0 ff000000 )' % i for i in range(len(images))]
    out += ['\t)', '\tlight_materials ( 0 )', '\tlight_model_cfgs ( 1', '\t\tlight_model_cfg ( 00000000',
            '\t\t\tuv_ops ( 1', '\t\t\t\tuv_op_copy ( 1 0 )', '\t\t\t)', '\t\t)', '\t)',
            '\tvtx_states ( 3', '\t\tvtx_state ( 00000000 0 -5 0 00000002 )',
            '\t\tvtx_state ( 00000000 0 -6 0 00000002 )', '\t\tvtx_state ( 00000000 0 -7 0 00000002 )', '\t)']
    states = [('Diffuse', 0, [0], 0), ('Sheen', 0, [0], 1), ('Glint', 0, [0], 2),
              ('Paint', 1, [1, 4, 6], 0), ('Metal', 1, [2, 5, 6], 0), ('Glass', 2, [3], 0), ('Ground', 0, [0], 0)]
    out += ['\tprim_states ( %d' % len(states)]
    for name, shader, tex, vtx in states:
        out += ['\t\tprim_state %s ( 00000000 %d' % (name, shader),
                '\t\t\ttex_idxs ( %d %s ) 0 %d 0 0 1' % (len(tex), ' '.join(map(str, tex)), vtx), '\t\t)']
    faces = sum(len(t) // 3 for _, t in primitives)
    idx = sum(len(t) for _, t in primitives)
    out += ['\t)', '\tlod_controls ( 1', '\t\tlod_control (', '\t\t\tdistance_levels_header ( 0 )',
            '\t\t\tdistance_levels ( 1', '\t\t\t\tdistance_level (', '\t\t\t\t\tdistance_level_header (',
            '\t\t\t\t\t\tdlevel_selection ( 2000 )', '\t\t\t\t\t\thierarchy ( 1 -1 )', '\t\t\t\t\t)',
            '\t\t\t\t\tsub_objects ( 1', '\t\t\t\t\t\tsub_object (',
            '\t\t\t\t\t\t\tsub_object_header ( 00000400 -1 -1 000001d2 000001c4',
            '\t\t\t\t\t\t\t\tgeometry_info ( %d %d 0 %d 0 0 %d 0 0 0' % (faces, len(primitives), idx, len(primitives)),
            '\t\t\t\t\t\t\t\t\tgeometry_nodes ( 1',
            '\t\t\t\t\t\t\t\t\t\tgeometry_node ( %d 0 0 0 0' % len(primitives),
            '\t\t\t\t\t\t\t\t\t\t\tcullable_prims ( %d %d %d )' % (len(primitives), faces, idx),
            '\t\t\t\t\t\t\t\t\t\t)', '\t\t\t\t\t\t\t\t\t)', '\t\t\t\t\t\t\t\t\tgeometry_node_map ( 1 0 )',
            '\t\t\t\t\t\t\t\t)', '\t\t\t\t\t\t\t\tsubobject_shaders ( 3 0 1 2 )',
            '\t\t\t\t\t\t\t\tsubobject_light_cfgs ( 1 0 ) 0', '\t\t\t\t\t\t\t)',
            '\t\t\t\t\t\t\tvertices ( %d' % len(vertices)]
    for i in range(len(vertices)):
        out += ['\t\t\t\t\t\t\t\tvertex ( 00000000 %d %d ffffffff ff000000' % (i, i),
                '\t\t\t\t\t\t\t\t\tvertex_uvs ( 1 %d )' % i, '\t\t\t\t\t\t\t\t)']
    out += ['\t\t\t\t\t\t\t)', '\t\t\t\t\t\t\tvertex_sets ( 1',
            '\t\t\t\t\t\t\t\tvertex_set ( 0 0 %d )' % len(vertices), '\t\t\t\t\t\t\t)',
            '\t\t\t\t\t\t\tprimitives ( %d' % (2 * len(primitives))]
    for state, tris in primitives:
        out += ['\t\t\t\t\t\t\t\tprim_state_idx ( %d )' % state, '\t\t\t\t\t\t\t\tindexed_trilist (',
                '\t\t\t\t\t\t\t\t\tvertex_idxs ( %d %s )' % (len(tris), ' '.join(map(str, tris))),
                '\t\t\t\t\t\t\t\t\tnormal_idxs ( %d %s )' % (len(tris) // 3,
                                                          ' '.join('%d 3' % tris[k] for k in range(0, len(tris), 3))),
                '\t\t\t\t\t\t\t\t\tflags ( %d %s )' % (len(tris) // 3, ' '.join(['00000000'] * (len(tris) // 3))),
                '\t\t\t\t\t\t\t\t)']
    out += ['\t\t\t\t\t\t\t)', '\t\t\t\t\t\t)', '\t\t\t\t\t)', '\t\t\t\t)', '\t\t\t)', '\t\t)', '\t)', ')', '']
    write_unicode(os.path.join(directory, 'material-spheres.s'), out)
    sd = ['SIMISA@@@@@@@@@@JINX0t1t______', '', 'shape ( material-spheres.s',
          '\tESD_Detail_Level ( 0 )', '\tESD_Alternative_Texture ( 0 )',
          '\tESD_Bounding_Box ( -10 0 -3.5 10 2.6 3.5 )',
          '\tESD_TSREMaterial ( spheres_paint.png 1 1 1 1 1 1 1 1 1 0.03 )',
          '\tESD_TSREMaterial ( spheres_glass.png 1 1 1 1 0 0.05 )', ')', '']
    write_unicode(os.path.join(directory, 'material-spheres.sd'), sd)


def write_unicode(path, lines):
    open(path, 'wb').write(b'\xff\xfe' + '\r\n'.join(lines).encode('utf-16-le'))


if __name__ == '__main__':
    directory = sys.argv[1]
    os.makedirs(directory, exist_ok=True)
    textures(directory)
    shape(directory)
    print('written to', directory)
