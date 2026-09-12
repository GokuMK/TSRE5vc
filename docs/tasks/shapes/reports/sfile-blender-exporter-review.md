# Blender MSTS/ORTS exporter: format evidence

Reviewed: 2026-09-11. Scope: static review of the Python exporter and its value-construction paths. No Blender export, native MSTS run, or TSRE implementation change was performed. The purpose is to improve the [shape format reference](../../../features/msts-shape-file-format.md), before deciding which questions require MSTS investigation.

Repository: `pwillard/Blender_MSTS_ORTS_Exporter`, snapshot `8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6` (2026-09-10). The local review checkout is `/tmp/tsre-blender-exporter-review`; no exporter code or example assets were added to TSRE. Links below pin the inspected revision of `io_export_mstsexporter/export_msts.py`.

These findings establish what this exporter writes. They provide practical evidence and useful hypotheses, but do not establish the full MSTS specification or prove that every exporter path works.

## 1. Triangle normals are pairs, not one scalar per declared entry

`Primitive.Write` writes:

- `vertex_idxs`: count `3 × triangle count`, followed by the triangle vertex indices.
- `normal_idxs`: count `triangle count`, followed by a pair `(normal index, 3)` per triangle.
- `flags`: count `triangle count`, followed by one zero DWORD per normal/triangle.

`AddTriangleToSubObject` constructs each face normal from the triangle normal, transforms and normalizes it, and adds its global normal-table index to the primitive. Its comment attributes these normals to MSTS culling. The writer proves the pair layout for its triangle output; the interpretation of the second word as a covered-vertex/run length is plausible, but other values and native usage still need confirmation.

Consequently `normal_idxs ( 1 0 3 )` is intentional output here. Treating the count as the number of following scalar words would lose the second word. [Face-normal construction](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L1463-L1542), [primitive serialization](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L2439-L2484).

## 2. The primitive count includes state changes

`SubObject.WritePrimitives` counts each draw primitive and each necessary `prim_state_idx` transition. It suppresses redundant state-change blocks when consecutive primitives share the same state. The declared count therefore equals emitted child commands, not just emitted triangle lists, for this writer.

For two triangle lists using the same state, the count is three: one state change and two draws. If their states differ, it is four. This corrects the initial reference's overly broad suggestion that state changes necessarily sit outside the count. Permissive reading of inconsistent source counts remains a separate recovery policy. [Writer](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L2665-L2688).

## 3. Geometry counters and map construction become more concrete

`WriteSubObjectGeometryInfo` emits exactly ten scalar counters:

| Position | Exporter value |
|---|---|
| 0 | Total triangles in the subobject |
| 1 | Number of nonempty vertex sets |
| 2 | Zero |
| 3 | Total triangle vertex indices, `3 × triangles` |
| 4, 5 | Zero |
| 6 | Number of emitted indexed triangle lists |
| 7, 8, 9 | Zero |

This independently supports the ten-word layout used by TSRE and Open Rails. It does not resolve the erroneous/repeated names in the eleven-word FFEDIT production, or explain the zero fields. The exporter itself comments that several of these fields have unknown uses.

It creates one `geometry_node` for each matrix used by draw primitives in this subobject. `geometry_node_map` has one entry per shape matrix: the corresponding geometry-node index, or `-1` when no node was created. This is a matrix-indexed map in this output, not an arbitrary hierarchy array.

For each emitted node, it writes:

- `geometry_node`: number of distinct referenced vertex states for that matrix, then four zeros.
- `cullable_prims`: number of primitive lists, number of triangles, number of triangle vertex indices for that matrix.

Thus the writer uses the grammar's `NumFlatSections` position for triangle count. Whether a flat section always means one triangle, and how lines/points or other exporters populate it, remains unknown. [Counter/map generation](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L2539-L2604).

## 4. Useful lighting codes and texture conventions

The exporter exposes these `vtx_state.LightMatIdx` choices:

| UI choice | Code |
|---|---:|
| Normal | -5 |
| Specular 25 | -6 |
| Specular 750 | -7 |
| Full bright | -8 |
| Cruciform | -9 |
| Half bright | -11 |
| Dark | -12 |
| Emissive | -5, with a different shader selection |

These are exporter labels, not independently verified native equations. In particular, emissive is not represented by a unique negative lighting code. [Lighting mapping](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L728-L773).

The material builder makes the following selections:

| Material | Shader | Alpha-test mode |
|---|---|---:|
| Normal lighting, opaque | `TexDiff` | 0 |
| Normal lighting, clip | `BlendATexDiff` | 1 |
| Normal lighting, blended/sorted | `BlendATexDiff` | 0 |
| Emissive, opaque | `Tex` | 0 |
| Emissive, clip | `BlendATex` | 1 |
| Emissive, blended/sorted | `BlendATex` | 0 |

`ZBufMode` defaults to 1. These combinations describe exporter output, not all legal shader/alpha/depth modes. [Material construction](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L1621-L1657), [state writer](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L2386-L2408).

`texture`'s second field is populated by `iFilterAdd('MipLinear')`: it is a reference into `texture_filter_names` in this exporter. This is stronger evidence than an unexplained numeric selector. `uv_op_copy` uses texture address mode 1; nearby comments map 1 to repeat, 2 to mirror, 3 to edge extension, and 4 to border clamping. Only 1 is selected by the inspected material-builder path; 2–4 remain comment evidence. [Filter indexing](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L841-L892), [addressing](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L1621-L1627).

## 5. Subobject flags correlate with sorting and specular choices

The ordinary header prefix is `00000400 -1 -1 000001d2 000001c4`. Alpha-sorted materials use `00000500 0 0 000001d2 000001c4`. Specular options clear bit `0x400` from the flags word. These correlations give us targeted native-code questions, but they do not prove complete bit definitions: the sorted path also changes sort-vector/volume references.

The exporter orders subobjects by a nonserialized priority: ordinary 0, alpha-blended 1, alpha-sorted 2. So draw order is partly encoded by the subobject sequence, not by a `Priority` field in the shape. It writes `SubObjID` as scalar zero. [Material flags](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L1583-L1604), [sorting](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L1870), [SubObjID](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L2627-L2628).

## 6. Coordinate conversion and direct quaternion keys

The exporter converts Blender points and translation keys `(x,y,z)` to `(x,z,y)`, and UV coordinates `(u,v)` to `(u,1-v)`. Matrix serialization explicitly rearranges basis/translation components and leaves root matrices at identity. These are Blender-to-file conventions, not a reason to apply another axis swap to already loaded MSTS data. [UV conversion](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L1156-L1159), [matrices/points](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L1366-L1407).

Rotation controllers emit `tcb_rot` containing `slerp_rot(frame,x,y,z,w)` keys. Quaternion components are reordered to file `(x,z,y,w)`; Euler inputs first convert to a quaternion. This is independent writer evidence for the direct five-scalar `slerp_rot` layout. The exporter casts frames to integers and comments that FFEDIT compression needs that. It selects frame rate 30, an exporter choice, not a format constant. This review does not validate the quaternion handedness/conversion mathematically or in native MSTS. [Key generation](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L1981-L2032), [key/controller writers](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L2759-L2860).

## 7. Label handling is not a reliable syntax reference

`MSTSName` replaces dots with underscores but does not quote or remove spaces. Matrix labels use that function; animation labels use the original object name. Primitive-state labels concatenate matrix labels and image stems. All three writers interpolate labels directly without quoting.

This can produce invalid unquoted multi-word labels and can make a dotted object's matrix label differ from its animation label. It is a plausible mechanism for this class of malformed file; it does not prove that the previously examined coaches were created by this exporter. Do not adopt this behavior as valid native syntax. [Name transformation](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L1358-L1375), [animation label](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L2037-L2041), [label writers](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L2299-L2319).

## 8. Limits of this evidence

The implemented material path uses one source UV channel and one image texture, so it cannot explain the full multi-texture grammar. Defaults such as `ffffffff`/`ff000000` vertex colours, zero face flags, `LightFlags=2`, zero SubObjID and fixed vertex-format masks do not reveal their complete meanings.

Two visible inconsistencies further caution against treating all code as tested specification: `iLightConfigAdd` refers to `op` rather than `eachop` in its reflection branch, and `iPrimStateAdd` assigns lowercase `zBias` while serialization reads uppercase `ZBias`. The normal material builder requests zero bias, so that latter mismatch can stay hidden. These are static observations only; no upstream bug fix or execution claim is made. [UV-config builder](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L914-L933), [state builder](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/8bf7825c5cd90d7fe595b75b05aaf60252e6a0a6/io_export_mstsexporter/export_msts.py#L1024-L1057).

The exporter narrows our questions substantially. Native investigation is still needed for general flag/enum meanings, nontriangle normal records, geometry zero fields, TCB behavior, complex-box semantics and alternate LOD/UV paths.
