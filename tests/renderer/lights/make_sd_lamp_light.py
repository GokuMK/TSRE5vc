# The street lamp's light as an S shape's .sd light (task shapes 14), to
# check that an ESD_TSRELight lights the scene as the glTF light does:
#   tsre_street_lamp_dark.gltf   the lamp without its KHR_lights_punctual light
#   tsre_street_lamp_light.s/.sd a light-only S shape: two 1 cm quads hidden in
#                                the lamp's head, and the glTF light's values in
#                                an ESD_TSRELight. An S shape is drawn through its
#                                root with x mirrored (MSTS is left-handed): the
#                                node's translation with x negated, its rotation
#                                as QDirection with y and z negated.
#   sd-lamp-views.json           lamp-views.json with each lamp as the dark lamp
#                                and the light-only shape at its place
# Usage: python3 make_sd_lamp_light.py <tests/renderer/lights>
import copy, json, math, os, sys

here = sys.argv[1]
lamp = json.load(open(os.path.join(here, "tsre_street_lamp.gltf")))
light = lamp["extensions"]["KHR_lights_punctual"]["lights"][0]
node = next(n for n in lamp["nodes"] if "KHR_lights_punctual" in n.get("extensions", {}))

dark = copy.deepcopy(lamp)
del dark["extensions"]["KHR_lights_punctual"]
dark["extensionsUsed"].remove("KHR_lights_punctual")
for n in dark["nodes"]:
    n.get("extensions", {}).pop("KHR_lights_punctual", None)
    if n.get("extensions") == {}:
        del n["extensions"]
dark["nodes"] = [n for n in dark["nodes"] if "mesh" in n]
dark["scenes"] = [{"nodes": list(range(len(dark["nodes"])))}]
json.dump(dark, open(os.path.join(here, "tsre_street_lamp_dark.gltf"), "w"), indent=1)

def simis(path, text):
    # MSTS text files: UTF-16 little endian with a byte order mark, CRLF.
    with open(path, "wb") as f:
        f.write(b"\xff\xfe" + text.replace("\n", "\r\n").encode("utf-16-le"))

# Two quads (MAIN and PART), 1 cm each, inside the head (glTF's 1.25, 5.95,
# 0: x negated in the S shape).
def p(x, y):
    return "point ( %g %g 0 )" % (-1.26 + 0.005 * x, 5.94 + 0.01 * y)
points = " ".join(p(x, y) for x, y in ((0, 0), (1, 0), (1, 1), (0, 1), (2, 0), (3, 0), (3, 1), (2, 1)))
shape = """SIMISA@@@@@@@@@@JINX0s1t______

shape (
 shape_header ( 00000000 00000000 )
 shader_names ( 1 named_shader ( TexDiff ) )
 texture_filter_names ( 1 named_filter_mode ( MipLinear ) )
 points ( 8 %s )
 uv_points ( 4 uv_point ( 0 1 ) uv_point ( 1 1 ) uv_point ( 1 0 ) uv_point ( 0 0 ) )
 normals ( 1 vector ( 0 0 1 ) )
 sort_vectors ( 0 ) colours ( 0 )
 matrices ( 2 matrix MAIN ( 1 0 0 0 1 0 0 0 1 0 0 0 ) matrix PART ( 1 0 0 0 1 0 0 0 1 0 0 0 ) )
 images ( 1 image ( tsre_street_lamp_light.ace ) )
 textures ( 1 texture ( 0 0 0 ff000000 ) )
 light_materials ( 0 )
 light_model_cfgs ( 1 light_model_cfg ( 00000000 uv_ops ( 1 uv_op_copy ( 1 0 ) ) ) )
 vtx_states ( 2 vtx_state ( 00000000 0 -5 0 00000002 ) vtx_state ( 00000000 1 -5 0 00000002 ) )
 prim_states ( 2 prim_state Body ( 00000000 0 tex_idxs ( 1 0 ) 0 0 0 0 1 )
  prim_state Part ( 00000000 0 tex_idxs ( 1 0 ) 0 1 0 0 1 ) )
 lod_controls ( 1 lod_control ( distance_levels_header ( 0 ) distance_levels ( 1
 distance_level ( distance_level_header ( dlevel_selection ( 1000 ) hierarchy ( 2 -1 0 ) )
 sub_objects ( 1 sub_object ( sub_object_header ( 00000400 -1 -1 000001d2 000001c4
  geometry_info ( 4 2 0 12 0 0 2 0 0 0 geometry_nodes ( 1 geometry_node ( 2 0 0 0 0 cullable_prims ( 2 4 12 ) ) )
   geometry_node_map ( 2 0 0 ) )
  subobject_shaders ( 1 0 ) subobject_light_cfgs ( 1 0 ) 0 )
 vertices ( 8 vertex ( 00000000 0 0 ffffffff ff000000 vertex_uvs ( 1 0 ) )
  vertex ( 00000000 1 0 ffffffff ff000000 vertex_uvs ( 1 1 ) )
  vertex ( 00000000 2 0 ffffffff ff000000 vertex_uvs ( 1 2 ) )
  vertex ( 00000000 3 0 ffffffff ff000000 vertex_uvs ( 1 3 ) )
  vertex ( 00000000 4 0 ffffffff ff000000 vertex_uvs ( 1 0 ) )
  vertex ( 00000000 5 0 ffffffff ff000000 vertex_uvs ( 1 1 ) )
  vertex ( 00000000 6 0 ffffffff ff000000 vertex_uvs ( 1 2 ) )
  vertex ( 00000000 7 0 ffffffff ff000000 vertex_uvs ( 1 3 ) ) )
 vertex_sets ( 1 vertex_set ( 0 0 8 ) )
 primitives ( 4
  prim_state_idx ( 0 ) indexed_trilist ( vertex_idxs ( 6 0 1 2 0 2 3 ) normal_idxs ( 2 0 3 0 3 ) flags ( 2 00000000 00000000 ) )
  prim_state_idx ( 1 ) indexed_trilist ( vertex_idxs ( 6 4 5 6 4 6 7 ) normal_idxs ( 2 0 3 0 3 ) flags ( 2 00000000 00000000 ) ) )
 ) ) ) ) ) )
)
""" % points
simis(os.path.join(here, "tsre_street_lamp_light.s"), shape)

spot = light.get("spot", {})
# glTF's right-handed space to the S shape's (x mirrored by its root).
tx, ty, tz = node["translation"]
qx, qy, qz, qw = node["rotation"]
mirrored_translation = (-tx, ty, tz)
mirrored_rotation = (qx, -qy + 0.0, -qz + 0.0, qw)  # (no -0)
sd = """SIMISA@@@@@@@@@@JINX0t1t______

shape ( tsre_street_lamp_light.s
\tESD_Detail_Level ( 0 )
\tESD_Alternative_Texture ( 0 )
\tESD_TSRELight (
\t\tName ( "%s" )
\t\tType ( %s )
\t\tPosition ( %s )
\t\tQDirection ( %s )
\t\tColor3F ( %s )
\t\tIntensity ( %g )
\t\tRange ( %g )
\t\tCone ( %.4f %.4f )
\t)
)
""" % (light["name"], light["type"].capitalize(),
       " ".join("%g" % v for v in mirrored_translation),
       " ".join("%.8f" % v for v in mirrored_rotation),
       " ".join("%g" % v for v in light["color"]),
       light["intensity"], light["range"],
       math.degrees(spot.get("innerConeAngle", 0.0)), math.degrees(spot.get("outerConeAngle", math.pi / 4)))
simis(os.path.join(here, "tsre_street_lamp_light.sd"), sd)

views = json.load(open(os.path.join(here, "lamp-views.json")))
views["notes"] = ("lamp-views.json with each lamp as tsre_street_lamp_dark.gltf and the light-only "
                  "tsre_street_lamp_light.s, whose .sd light (ESD_TSRELight, task shapes 14) is the glTF "
                  "lamp's light: the captures should match lamp-views.json's. Game root from lamp-root.sh.")
views["output"] = "build/renderer-lights-sd"
objects = []
for o in views["objects"]:
    if o.get("file") != "tsre_street_lamp.gltf":
        objects.append(o)
        continue
    objects.append(dict(o, file="tsre_street_lamp_dark.gltf"))
    objects.append(dict(o, file="tsre_street_lamp_light.s"))
views["objects"] = objects
json.dump(views, open(os.path.join(here, "sd-lamp-views.json"), "w"), indent=2)
print("written: tsre_street_lamp_dark.gltf, tsre_street_lamp_light.s/.sd, sd-lamp-views.json")
