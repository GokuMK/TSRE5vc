# Test content for shape emitters (task shapes 15):
#   tsre_smoke_demo.s/.sd  an S shape of two 1 cm quads at its foot, its .sd
#                          giving a chimney 8 m up (smoke) and a steam vent
#                          1 m up beside it, both with Variation ( 0.3 )
#   emitter-views.json     six of them in a row on a route, for the capture
#                          harness (their plumes unlike), a street and an
#                          aerial view; game root from emitter-root.sh
# Usage: python3 make_smoke_demo.py <tests/renderer/emitters>
import json, os, sys

here = sys.argv[1]

def simis(path, text):
    # MSTS text files: UTF-16 little endian with a byte order mark, CRLF.
    with open(path, "wb") as f:
        f.write(b"\xff\xfe" + text.replace("\n", "\r\n").encode("utf-16-le"))

def p(x, y):
    return "point ( %g %g 0 )" % (0.005 * x, 0.01 * y)
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
 images ( 1 image ( tsre_smoke_demo.ace ) )
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
simis(os.path.join(here, "tsre_smoke_demo.s"), shape)
simis(os.path.join(here, "tsre_smoke_demo.sd"), """SIMISA@@@@@@@@@@JINX0t1t______

shape ( tsre_smoke_demo.s
\tESD_Detail_Level ( 0 )
\tESD_Alternative_Texture ( 0 )
\tESD_TSREEmitter (
\t\tName ( "CHIMNEY" )
\t\tPosition ( 0 8 0 )
\t\tDiameter ( 0.4 )
\t\tKind ( Exhaust )
\t\tFlow ( 0.08 )
\t\tExitSpeed ( 2 )
\t\tTemperature ( 150 )
\t\tVapour ( 0.2 )
\t\tSoot ( 0.5 )
\t\tVariation ( 0.3 )
\t)
\tESD_TSREEmitter (
\t\tName ( "VENT" )
\t\tPosition ( 3 1 0 )
\t\tDiameter ( 0.15 )
\t\tKind ( Steam )
\t\tFlow ( 0.05 )
\t\tExitSpeed ( 8 )
\t\tTemperature ( 100 )
\t\tVariation ( 0.3 )
\t)
)
""")

views = {
    "notes": "Six smoke demo shapes in a row, placed for the capture only (task shapes 15): each a chimney 8 m up and a steam vent, Variation 0.3, so their plumes differ. The route must find tsre_smoke_demo.s/.sd in its SHAPES directory: emitter-root.sh builds a game root of links with them. Plumes need the frames to grow: settle runs 60 frames.",
    "output": "build/renderer-emitters",
    "width": 960, "height": 540, "pickGrid": [2, 2], "timingFrames": 1, "diffTolerance": 16, "shadows": True,
    "settle": {"minFrames": 60, "stableFrames": 2, "maxFrames": 60, "maxSeconds": 240},
    "views": [
        {"name": "street", "offset": [0, 0, 0], "aboveGround": 2.5, "rot": [1.5708, -0.05]},
        {"name": "above", "offset": [40, 0, -60], "aboveGround": 30, "rot": [0.6, -0.45]},
    ],
    "objects": [{"file": "tsre_smoke_demo.s", "offset": [25 + 10 * i, 0, -15], "yaw": 0} for i in range(6)],
}
json.dump(views, open(os.path.join(here, "emitter-views.json"), "w"), indent=2)
print("written: tsre_smoke_demo.s/.sd, emitter-views.json")
