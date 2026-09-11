# SFileComplex format coverage

Implementation: `SFileDocument.cpp`, independently described from the original shape grammar and checked with the authored `tests/shapes/fixtures/coverage.s`. This is a storage/serialization ledger; renderer support is narrower. The fixture contains deliberately distinguishable values, labels, optional fields both present and absent, and repeated records. Its text/binary × compressed/uncompressed round trips must reproduce the canonical binary document.

The retention column describes **Complete** mode. Numeric components now occupy contiguous native-word arrays with compact type tags; 32-byte block records retain hierarchy, optional presence and scalar/child ordering. Labels, unknown text and opaque payloads use separate storage. There is no document object per coordinate/index. Known text numbers normalize to the shape format's native 32-bit widths; original numeric spelling is not retained. Exceptional/unsupported values remain available as text.

A pre-load Compact request skips blocks unused by the implemented runtime, including source-only tables, normal-index/face-flag lists, vertex sets and unknown extensions. Regular points, normals, UV coordinates and vertices use packed numeric tables in Compact, avoiding per-row block records and unused vertex fields. Unusual or incomplete tables fall back to the general permissive parser; runtime reference/coordinate validation still applies. Other required records use the typed model. Extraction builds CPU rendering arrays; indices are not expanded into triangle caches until `initGL()`. The temporary Compact document is released after successful CPU extraction, while upload arrays survive until successful GL initialization. Full save requires a Complete reload.

| Family / records | Retained and serialized fields | Runtime use | Tests |
| --- | --- | --- | --- |
| `shape`, `shape_header`, `volumes`, `vol_sphere`, `vector` | Both header flags, optional second flag; sphere center/radius; labels and nested/repeated blocks | Bounds currently derive from points or `.sd`; volume data preserved | `coverage.s`, stock corpus |
| `shader_names`, `texture_filter_names`, `named_shader`, `named_filter_mode` | Counts and full names | TexDiff alpha convention; remaining names retained | `coverage.s`, stock |
| Points, UVs, normals, sort vectors, colours | Counts and all float components, including unused table entries | Position, normal and first UV set | `coverage.s`, stock |
| Matrices | Names and all 12 source values | Resolved hierarchy, mirrored root, per-instance transforms | `coverage.s`, stock, CPU bounds tests |
| Images and textures | Image strings; image/filter references, mip bias and optional border colour | First texture of a primitive; shared TexLib cache | `coverage.s`, textured stock comparison |
| Light materials | Flags, all four colour references, specular power | Preserved; current TSRE light-state conventions applied separately | `coverage.s` |
| Light model configurations / UV operations | Configuration flags; every original grammar UV operation and all scalar parameters, including callback IDs | UV effects/callbacks are preserved, not evaluated | `coverage.s` includes all 15 UV operation types |
| Vertex states | Flags, matrix, light material/configuration/flags, optional second matrix | First matrix and legacy light-state conventions | `coverage.s`, stock |
| Primitive states / texture indices | Flags, shader, every texture index, bias, vertex state; optional alpha/light/Z fields | First texture and vertex state; alpha test | `coverage.s`, stock |
| Vertices / UV lists / vertex sets | Both colour fields, all UV indices, optional weight; vertex set references/ranges | Source retained until GL initialization; packed renderer vertices are separate | `coverage.s`, Complete/Compact tests |
| Primitives | Ordered state changes; triangles, lines and point ranges; original vertex/normal index lists and face flags | Triangle/line/point packets; normal-index and face-flag source remains retained | `coverage.s`, synthetic GL and stock triangles |
| Geometry / subobject headers | All header flags/references, ten geometry counters, five node counters, culling fields, maps, optional shader/light lists and SubObjID | Geometry maps drive named visibility; counters remain source data | `coverage.s`, stock |
| LOD | All controls, optional scale, bias, distances, hierarchy and subobjects | Explicit distance-level selection; first level per control may be preselected | `coverage.s`, first-LOD/reload tests, stock |
| Animation | Every animation/node/controller, frame values, all TCB parameters, linear keys; direct quaternion-key `slerp_rot` variant | First animation, linear position / quaternion interpolation. Full TCB weighting is preserved but not evaluated; ambiguous grammar `slerp_rot` vector form is preserved | `coverage.s`, synthetic direct-key variant, animated stock comparison |
| Named data | Name count, named geometry strings, every reference's five fields | Preserved; no runtime consumer | `coverage.s` |
| Unknown text | Nested blocks, labels, quoted strings and unquoted atoms | Ignored by rendering | Same-encoding round trip, known sibling edit |
| Unknown binary / extra payload | Full bounded payload and token ID; trailing bytes remain attached to their record | Ignored by rendering | Unknown block and extra-tail tests, known-field edit |
| Extra roots | Additional framed roots after the main shape | Main shape drives rendering | Seven stock wiper files |
| `.sd` metadata | Full text tree; detail, alternative textures, bounding/complex boxes, snap extension, unknown records | Bounds helpers, seasonal texture path, snap points. Complex-box orientation is retained; current bounds helpers use its axis-aligned dimensions | Metadata edit/save tests, adjacent stock metadata |

Binary scalar storage distinguishes unsigned, signed, hexadecimal DWORD, float32 and UTF-16 strings. Original float32 bits survive binary round trips. Text normalization may add quotes or change formatting; source indices and meaningful record order remain intact. Original comments/whitespace are not retained.

Unknown cross-encoding conversion fails explicitly when no lossless schema exists. Damaged/truncated documents retain recovered content and diagnostics but refuse saving. Semantically Broken rendering data can remain a complete, serializable source document; serialization is not a repair operation.

Limits: 512 MiB input/inflated envelope, bounded decompression, 128 levels of document nesting, scalar/index/range validation before GL. Counts do not determine traversal; allocation hints for visited numeric tables are capped by their actual bounded payload, never trusted alone. Binary child lengths delimit recovery; irrecoverable text framing records an incomplete document.
