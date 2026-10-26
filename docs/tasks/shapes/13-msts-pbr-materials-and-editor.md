# Task 13 - PBR Materials for MSTS Shapes, and a Materials Editor

Status: proposal (2026-10-27), on `feature/shape-materials`; decisions for
the user at the end.

## Where we are

- Open Rails PR #1251 (open, "MSTS legacy shaders": DetailMod2X, GlossMap,
  AlphRefMap, NightLight, baked vertex colours, UV operations, a second
  texture stage) is noted in task renderer 35: none of our 44,000 shapes
  uses those shader names, so not now.
- MSTS specular is not drawn. Light materials -6 (Specular 25) and -7
  (Specular 750) shade as plain diffuse; `StandardFog.fs` declares a
  `specularColor` that nothing sets (task renderer 35 noted it as a task of
  its own). That is why the scene shows nothing new.
- glTF shapes have full metallic-roughness PBR (task 06, `PbrShading.glsl`):
  base colour, metallic-roughness, normal, occlusion and emissive maps,
  clearcoat, specular, transmission, image-based light from the environment
  map (task renderer 17), the sun, local lights and shadows; QRhi and
  OpenGL. MSTS shapes do not read the environment map yet.
- The S format already gives a primitive state several textures
  (`prim_state ( ... tex_idxs ( n i0 i1 ... ) ... )`); DetailMod2X and
  GlossMap use two.

## 1. A PBR material for S files

The user's idea: a "ReflectionMap" shader with three textures (diffuse,
how much it reflects, normal map), the reflection direction from the
normal map. That is right: the reflected ray is reflect(-view, n), with n
the vertex normal turned by the normal map, and the environment map gives
what it sees. Proposed improvements:

- **The glTF PBR shading, not a shader of its own.** The same look for MSTS
  and glTF content, the sun's and lamps' highlights as well as the
  reflections, blurred reflections by roughness (the environment map's
  prefiltered levels), Fresnel (more reflection at grazing angles: car
  paint, windows), energy conserving. One code path to keep.
- **The second texture packed as glTF's ORM:** R occlusion (baked dirt and
  creases), G roughness, B metalness. "How much it reflects" is metalness
  and roughness together; Fresnel adds the reflection of paint and glass.
  Blender and Substance bake exactly this, and a glTF material maps one to
  one.
- **Normal maps in tangent space (MikkTSpace, Blender's baker).** MSTS shapes
  carry no tangents: generated at load for the parts with a normal map.
- **Factors per material**, so a texture can be left out: base colour tint,
  metalness, roughness, normal strength, occlusion strength; **clearcoat**
  strength and roughness (the varnish over painted bodies, the most visible
  effect on rolling stock); and **reflective glass**: blended parts (windows)
  keep their alpha and reflect.
- Not now: emissive maps (night textures do that, task renderer 35),
  refraction (costly; windows read right as reflective blended glass),
  anisotropy (brushed metal), sheen.

### In the S file

- **Shader names:** `PbrTexDiff` (opaque, alpha-tested by the primitive
  state as now) and `BlendAPbrTexDiff` (blended). Open Rails gives an
  unknown name no options and still draws `tex_idxs[0]` (`Shapes.cs`,
  `ShaderNames`): the diffuse texture shows, lit roughly. MSTS: unknown, to
  test with one shape.
- **Textures:** `tex_idxs` with three entries: base colour (alpha as now),
  ORM, normal. A missing one is -1, or the list is shorter.
- **Parameters:** the S format has no place for them (light materials hold
  colours and a specular power). Proposal: TSRE's sidecar of task 03,
  extended to S files: `shape.s.tsre.json`, a `materials` object keyed by
  the primitive state's name (else its index), with the factors. MSTS and
  Open Rails never read it; without it the defaults apply.
- Considered: parameters in the shader name (breaks the S text format's
  tokens), in the .sd (MSTS's parser; .sd is per shape anyway), in light
  material colours (a hack; light configs stay for MSTS lighting).

## 2. MSTS specular (-6, -7)

Small, and visible on existing content: a Blinn-Phong highlight of power 25
or 750 for those light materials, the sun's and the lamps'; before and after
captures of shapes using them (task renderer 35's note: -6 and -7 in GLOBAL
shapes).

## 3. Materials editor in the Shape Viewer

- **A material** is an S file primitive state: its shader name, textures,
  light material and configuration, alpha test, z-bias, depth mode, plus its
  PBR factors (sidecar).
- **Materials window:** the shape's materials (name, shader, texture
  thumbnails, the parts using each); add (a copy of the selected one or a
  default), remove (unused ones; or its parts move to another), edit with a
  description of each parameter beside it: shader, texture slots (from the
  shape's texture list or a file added to it), factors, alpha test, z-bias,
  MSTS lighting mode (-5 to -12 explained).
- **Assigning:** parts picked in the view (click, Ctrl adds, the hierarchy
  tree too), then Apply; an option to apply to the same parts in all
  distance levels (same matrix and material).
- **Live:** the parts redraw with the edited material at once.
- **Saving:** `SFileComplex::save` (text, binary, compressed) and the
  sidecar; undo of material edits within the session.

## 4. Selection IDs for shape parts

The editors share `SelectionIdCodec` (5-bit kind, 27-bit payload). The Shape
Viewer's consist preview uses raw ids today (car index + 1). Proposal, one
kind for both the Shape Viewer and the consist editor:

- `Kind::ShapePart`: payload = instance (11 bits: the car in a consist, 0
  for a single shape; 2047 cars) and part (16 bits: the primitive's index in
  the drawn distance level, counted over its meshes and parts in order;
  65535 parts; 0xffff the whole instance).
- `SelectionIdCodec::shapePart(instance, part)`, decoded as
  `primaryId = instance`, `part = part`.
- `SFileComplex::pushRenderItem` submits each part with
  `withPart(selection, partIndex)` when picking (packets shared between
  instances already take their id per submit).
- The consist editor picks cars with it (part ignored); the materials editor
  reads the part. glTF parts the same way later.

## Steps

1. `Kind::ShapePart` and per-part picking in the Shape Viewer and the
   consist preview; the picked part highlighted.
2. MSTS specular (-6, -7), with captures.
3. PBR S materials: the shader names, three textures, the sidecar's
   factors, generated tangents, into `RenderItem::Pbr`; both renderers.
4. The materials editor: window, assigning, saving, undo.
5. Tests: S round trip with PBR states (shape-complex), the sidecar,
   selection ids, Shape Viewer captures.

## Decisions for the user

1. The glTF PBR shading with an ORM second texture, instead of a
   reflection-only shader?
2. Shader names `PbrTexDiff` / `BlendAPbrTexDiff`?
3. Parameters in `shape.s.tsre.json`?
4. Clearcoat and reflective glass in the first step?
5. The selection id layout (11-bit instance, 16-bit part)?
