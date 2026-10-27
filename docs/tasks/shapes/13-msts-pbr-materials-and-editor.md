# Task 13 - PBR Materials for MSTS Shapes, and a Materials Editor

Status: implemented on `feature/shape-materials` (2026-10-27): steps 1-5
below; the user's review left (the editor by hand, specular on route track
shapes, their own maps). Translations of the new texts come with the merge.

## Where we are

- Open Rails PR #1251 (open, "MSTS legacy shaders": DetailMod2X, GlossMap,
  AlphRefMap, NightLight, baked vertex colours, UV operations, a second
  texture stage) is noted in task renderer 35: none of our 44,000 shapes
  uses those shader names, so not now.
- MSTS specular is not drawn. Light materials -6 (Specular 25) and -7
  (Specular 750) shade as plain diffuse; `StandardFog.fs` declares a
  `specularColor` that nothing sets. Many shapes use them, track shapes for
  their rails too.
- glTF shapes have full metallic-roughness PBR (task 06, `PbrShading.glsl`):
  base colour, metallic-roughness, normal, occlusion and emissive maps,
  clearcoat, specular, transmission (a copy of the frame read along the
  refracted ray), image-based light from the environment map (task renderer
  17), the sun, local lights and shadows; QRhi and OpenGL. Tangents are made
  at load when a file has none (`GltfShape.cpp`).
- The S format already gives a primitive state several textures
  (`prim_state ( ... tex_idxs ( n i0 i1 ... ) ... )`); DetailMod2X and
  GlossMap use two.

## Decisions (2026-10-27)

1. The glTF PBR shading for S files, with the second texture packed as
   glTF's ORM, instead of a reflection-only shader. The user's idea (diffuse,
   how much it reflects, normal map; the reflection direction from the normal
   map) is the same three textures: "how much it reflects" becomes roughness
   (how sharp) and metalness (metal or paint), occlusion comes as a bonus.
2. Shader names `PbrTexDiff` (opaque, alpha-tested by the primitive state as
   now) and `BlendAPbrTexDiff` (blended).
3. Factors in the `.sd`, short: `.sd` files are read for every shape of a
   route without the shape files, so one line a material, positional, only
   for materials that differ from the defaults, trailing defaults left out.
   No `.sd` or no line: the defaults. Keyed by the base colour texture's
   file name, not the primitive state's name: exporters name states after
   their matrix and look (`MAIN_SolidNorm`, `Bogie1_TransNorm`), the same
   name for many textures.
4. Clearcoat and reflective glass in the first step; transmission (the glTF
   one) as a factor too.
5. Selection ids: `Kind::ShapePart`, an 11-bit instance and a 16-bit part.
   Selector 14 of 32 (0-13 taken); ids are never saved, so the layout can
   change; an escape selector with a sub-kind if they ever run out.

Rejected for the factors: a new token in the S file (Open Rails reads S
blocks in a fixed order and its text reader does not skip an unexpected
block at a block's end: `SBR.cs`, `VerifyEndOfBlock`; binary files need a
token number the MSTS tools do not know); light materials (shared through
vertex states, several primitive states each; the part loses its MSTS
lighting mode); a JSON sidecar (lost when a shape is copied without it).

## 1. PBR material for S files

- **Textures** (`tex_idxs`, three entries; a missing one -1, or the list
  shorter):
  - 0 base colour (alpha as now): the shape's texture as it is; Open Rails
    and MSTS draw it.
  - 1 ORM(C): R occlusion, G roughness, B metalness, A clearcoat mask
    (optional: an opaque texture is clearcoat everywhere the factor says).
    One atlas holds painted bodies and bare bogies, so the varnish needs a
    mask.
  - 2 normal map, tangent space, OpenGL convention (green up), flat
    128,128,255.
- **Tangents** made at load for the parts with a normal map, per source
  vertex from the triangles' texture coordinate gradients, as glTF shapes
  do (shared code); +16 bytes a vertex for those parts.
- **The `.sd` line:**
  `ESD_TSREMaterial ( texture r g b a metalness roughness normal occlusion clearcoat clearcoatRoughness transmission )`
  keyed by the base colour texture's file name (lower case), written up to
  the last value that differs from its default: tint 1, metalness and
  roughness 1 with an ORM texture (they multiply its B and G) or 0 and 0.5
  without one (they are the values), normal and occlusion 1, the rest 0.
  Open Rails' `.sd` reader skips unknown tokens with their blocks
  (`STFReader.ParseBlock`), and Open Rails adds its own there
  (`ESD_ORTSSoundFileName`).
  - tint: multiplies the base colour texture.
  - metalness, roughness: multiply the ORM's B and G (alone without it).
  - normal: the normal map's strength; occlusion: the ORM's R strength.
  - clearcoat, its roughness: the varnish layer (times the ORM's A).
  - transmission: light through the surface (glass), drawn in the
    transmission pass.
- **Glass:** `BlendAPbrTexDiff` keeps its alpha and adds its reflection on
  top (Fresnel: clear head-on, a mirror at grazing angles); transmission
  above 0 tints and blurs what lies behind instead.
- Not now: emissive maps (night textures, task renderer 35), anisotropy,
  sheen.

### Preparing the textures

Base colour stays the shape's texture. The other maps can be baked
(Blender, Substance), made with Materialize or DeepBump from the image, or
generated by an image model one map at a time (roughness, metalness,
occlusion, normal, clearcoat mask; grayscale, same size and layout) and
packed into the ORM(C) texture. Values: glass roughness 0-5%, chrome and
polished steel 10-35%, clean paint 30-45%, weathered paint and roofs
60-80%, rust, dirt, rubber 85-100%; metalness white only for bare metal
(wheel treads, buffer faces, chrome), paint on metal black.

## 2. MSTS specular (-6, -7)

- -6 a broad sheen (power 25: painted bodies), -7 a tight glint (power 750:
  rail heads, chrome, glass). Only those parts change.
- Lit by our sun as it is (its colour and strength through the atmosphere,
  none at night), shadowed, faded in fog; not tied to Open Rails' white
  highlight. No setting: tuned here, with captures aimed at the sun.
- Far away the 750 glint widens and dims, so rails thinner than a pixel do
  not twinkle.
- No environment reflection on these parts: the PBR materials bring that.

## 3. Materials editor in the Shape Viewer

- **A material** is an S file primitive state: its shader name, textures,
  light material and configuration, alpha test, z-bias, depth mode, plus its
  PBR factors (`.sd`).
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
- **Saving:** `SFileComplex::save` (text, binary, compressed) and the `.sd`;
  undo of material edits within the session.

## 4. Selection IDs for shape parts

- `Kind::ShapePart` (selector 14): instance (11 bits: the car in a consist,
  0 for a single shape; 2047 cars) and part (16 bits: the primitive's index
  in the drawn distance level, counted over its sub-objects and parts in
  order, disabled sub-objects included).
- `SelectionIdCodec::shapePart(instance, part)`, decoded as
  `primaryId = instance`, `part = part`; `withPart` takes the 16-bit part.
- Shapes submit each part with its own id when the selection id is a
  ShapePart one (packets shared between instances take their id per
  submit).
- The consist editor picks cars with it (part ignored); the materials editor
  reads the part. glTF parts the same way.

## Implementation (2026-10-27)

- **Selection ids** (`SelectionId.h`): selector 14, `shapePart`,
  `forShapePart` (a ShapePart id takes the part, any other stays).
  `SFileLegacy`, `SFileComplex` and `GltfShape` submit each primitive with
  its number on such an id; `ComplexShape::drawnPrimitiveCount`,
  `drawnPrimitiveMaterial`, `pushPrimitiveHighlight` (a decal tint in the
  overlay layer). The Shape Viewer picks on a click (not a drag), Ctrl adds;
  the consist preview picks cars as instances (`Consist::pushRenderItems`).
- **Specular** (`MstsSpecular.glsl`, `RenderItem::Material::specularPower`,
  both renderers): normalized Blinn-Phong, `(p + 8) / 8 * (N.H)^p`, the
  sun's linear colour, shadowed, Schlick Fresnel from 0.25 (the sun; 0.08
  at first, too faint on rails: below) and 0.04 (the sky, `ambientColor`,
  reflected at grazing angles by
  smoothness); the power drops by the normals' spread over the pixel
  (screen derivatives) plus `(distance / 2000 m)^2`. Not in
  `StandardFast.fs` (the fast shaders) nor on untextured parts.
- **PBR** (`MstsMaterials`, both backends): `PbrTexDiff` opaque or
  alpha-tested at 0.51, `BlendAPbrTexDiff` blended glass
  (`RenderItem::Pbr::glass`: alpha covers the diffuse light; the coverage
  made up from the reflectance and highlights). A sub-object's PBR parts
  get a second mesh in the `RenderItem::PBR` layout with tangents per
  source vertex (`MstsMaterials::tangents`); the VNTA mesh keeps every part
  (highlight). The ORM's alpha times the clearcoat
  (`clearcoatInMetallicRoughnessAlpha`, bit 8192 of `pbrTextures`).
  ORM and normal maps load as `TexLib::Data` (never bled as cutouts,
  mipmaps the means of their values).
- **Generated tangents** pointed the bitangent down the image (texture
  coordinates grow down it): normal maps of glTF shapes without `TANGENT`
  read inside out. Fixed for both; Khronos NormalTangentTest now matches
  its reference.
- **Editing** (`SFileComplexMaterials.cpp`, Complete mode): `materialInfo`,
  `setMaterial` (shader and texture entries added when missing, a vertex
  state of its own when the light material changes and others share it),
  `addMaterial`, `removeMaterial` (unused; later states renumbered),
  `assignMaterial` (a `prim_state_idx` before the part and the old one
  back after it, redundant ones dropped, a state per matrix found or
  made, `subobject_shaders` completed), the `.sd` line (an `.sd` made when
  the shape has none), undo snapshots of both documents (20 steps, 256 MB).
  On new `SFileDocument::Node` operations: `setScalars`, `setLabel`,
  `insertCopy`, `removeChild`.
- **Materials window** (`ShapeMaterialsWindow`, View > Shape Materials):
  the list (name, shader, textures, lighting, matrix, parts), the form with
  a description under every field, Pack (an ORM(C) texture from grayscale
  maps, `MstsMaterials::packOrm`), picking and assigning, Undo. File >
  Save and Save As write the `.sd` too.
- **Tests:** `shape-materials` (24: factors, tangent direction, packing,
  editing, text and compressed binary round trips with the `.sd`, the
  legacy backend's PBR packets and part ids, specular power, the window);
  `selection-id` (19); `consist-preview-gl`, `shape-complex`(-gl),
  `gltf-pbr-gl` and the texture suites pass as before.
- **Test shape:** `scripts/make-material-spheres.py` writes six spheres
  (-5, -6, -7, clearcoated paint with an unvarnished stripe and rivets,
  metal with a rough band, glass); `tests/renderer/material-spheres.json`
  views them in the Shape Viewer (`TSRE_MATERIAL_SPHERES`).

### Findings

- Global track shapes use -6 for their rail heads (`A1t*`: `RailTops`, 165
  of 350 text shapes), -7 is rare (13); trainsets use -6 in 8 of 203 text
  shapes. Prudnik's track is mostly generated `GLOBAL/SHAPES/DB` shapes
  (`DB3f_a2t100mStrt`, `DB1s_a1t100mStrt` checked): every vertex state is
  -5, their rail tops (`mt_rtops`) included, and share it with the ballast,
  so they draw no highlight: an asset fix (a -6 state of their own for the
  rail tops), not TSRE's.
- In the Shape Viewer's studio light the -6 sheen is faint and the -7 glint
  a small spot; the route's sun is stronger. On EUROPE1's track at 15:00 and
  16:00 looking along it towards the sun (`tests/renderer/specular-views.json`,
  MSTS stock routes under `C:/MagiPacks/Microsoft Train Simulator`) the rail
  heads gain a sheen, most on the far rails (largest change 43 of 255,
  0.75% of the pixels): visible, not invasive.
- bbb at the user's camera (-5307,14961, west over the station throat,
  2026-10-27: "no difference at any time of day"): the shapes give the
  power right (`SFileLegacy` packets 25 on every A1t `RailTops`/`Railtop`,
  the main program's uniform set); painting every surface drawn with a
  specular power magenta showed the A1t rails from about 17 m out, and the
  foreground without: the A2tFrog7_5d crossing frogs there draw their
  `Railtop` with light material -5, so the content asks for no highlight
  (Open Rails and MSTS draw none either). No override by state name: such
  shapes are fixed in the assets (the user; those frogs are obsolete). The rest was too faint at a sun
  reflectance of 0.08 (rails +40-60 of 255); at 0.25 they gain up to 98 at
  16:00, looking towards the sun. Earlier Prudnik views missed the track
  (one beside the embankment, below the rails), and their "procedural
  track" explanation was wrong: Prudnik is mostly standard shapes.
- The user's Vectron set (AI-made maps; `MRCE_X4E-627_PBR.s` made with the
  editing API: base colour, packed ORM, normal map; clearcoat 1, its
  roughness 0.06, normal strength 0.7): the materials draw as meant; the
  maps show what their README warns of (a grey base colour where the body
  is black, painted marks as relief, drifted details).
- aaa at the user's camera (-5385,15368, the evening sun ahead down the
  line, 2026-10-27: "can't get any sun reflection at all"): only the
  `A1tPnt10dLft` points there have -6 rail tops (the `A2t500r20d` double
  track is all -5). Three things held their glint back: the specular took
  the lit shaders' visibility, which already holds N.L, and multiplied by
  N.L again (a 5 degree sun: 0.0076); Blinn-Phong's implicit visibility
  (N.L alone) dimmed it towards grazing angles, where the PBR sun's Smith
  term keeps it; and the lit shaders' sun fades by the horizon as a look
  (`Daylight::forElevation`, linear 0.05 of noon at 7 degrees, 0.0003 at
  1), so a sun low enough to glint along the rails had no light. Now the
  highlight takes the shadows alone, the Smith visibility, and the sun
  through the air where that is brighter (`Atmosphere::sunLight`, as the
  fog's light: `GLUU::specularColor`, the legacy `specularColor` uniform,
  0 under the moon, without time of day and in the Shape Viewer).
- That Vectron in the route view (Prudnik, `file0.act`, the user: plainer
  than in the Shape Viewer, no reflections to see): the route view loads
  S files with `SFileLegacy` on a worker and `adopt()` took every loaded
  field but the .sd's factors, so its materials drew with the defaults
  (no clearcoat, normal strength 1). The Shape Viewer window loads with
  `SFileComplex` and was right. Fixed; `shape-materials` loads the legacy
  shape as the loaders do. With the coat back the body mirrors the ballast
  and the horizon. Debug views of the shader's terms (the coat's
  reflection, the coat factor, the ORM's alpha) found it.
- Time of day (the user, bbb 4:00, 2026-10-27): the PBR profile was half
  as bright as the rest at dawn and dusk, and a time jump lit it up, then
  darkened it over some frames. Every PBR material (S files, glTF too):
  the ambient light took the cube scaled by the legacy ambient over the sky
  colour, which is the fog's and glows at dawn while the cube's zenith is
  still dark. Now scaled by the cube's own light from straight up: open
  sky gives the legacy ambient, the cube's level cancels (track at 4:00
  14 -> 35 of 255, the legacy track 30; noon unchanged). A jump left the
  cube with faces of two lightings (a face a frame): old reflections and a
  tilted light from half-updated faces. The route view now records the
  scene's light level (linear sun and sky colour) each face was drawn
  under and draws all six at once when one is 25 % off (about 10-15 ms on
  that frame); the track then holds its level from the jump frame on.
- PNG, JPG and TGA textures loaded on the calling thread
  (`ImageLib::IsThread` false; ACE and DDS on the loaders as
  `core.rendering.threadedTextureLoading` says): the user's six 1024 px PNG
  maps made their PBR profile's first generation take 200-290 ms (with ACE
  textures: under 50). Every `addTex` caller loading files was checked
  (2026-10-27): all cope with a texture still loading (shapes, glTF,
  profiles and `OglObj` draw once it is loaded; terrain textures, procedural
  terrain and brushes check `loaded` and retry, painting clones through a
  synchronous reload); map layers register textures in memory. So images
  follow the setting as ACE and DDS do, and a failed image load is marked on
  any thread (it was not on one: a missing PNG became a loaded 0x0 image).
- PBR occlusion maps did nothing on QRhi (the user: self shadowing weak,
  bbb, 2026-10-27): the night textures' unit (task 35, 13) is the PBR
  occlusion map's, and the binding loop gave it the night texture or the
  white dummy for every program. PBR programs now skip that branch
  (OpenGL binds the night texture only for parts that have one, and the
  PBR shaders declare none). The user's map is light (median 0.87) and
  occlusion darkens the ambient light only, so the track changes little.
- Shadows on PBR were half as strong (the user, bbb 16:00, 2026-10-27):
  PBR averaged the shadow map's samples (a half-occluded filter kept half
  the sun), the rest takes them at 1.6 times (0.8 far), so the shadow maps
  stand in for occlusion. The thin rails' shadows, mostly penumbra,
  darkened PBR ballast by 35 % (median) against 82 % on the legacy track.
  PBR now takes the same contrast (the user: as the rest of the content;
  vegetation keeps its own share, task 37): 59 %. GTAO may take that role
  later, not yet.

### Open

- The user's review: the window by hand, their own maps, the specular's
  strength (a lower sun, a sharper glint).
- MSTS and Open Rails with `PbrTexDiff` shapes (expected: MSTS fails to
  load them, Open Rails draws texture 0), not tried.
- Lamps' highlights on -6/-7 (the sun's only now).
- `BlendAPbrTexDiff` is glass: its reflection is added whole and covers
  what lies behind as much as it reflects, also where alpha is 0. Right for
  windows; a blended part whose transparent texels are smooth (a profile's
  feathered ballast edge with a smooth ORM there, a decal) would show a
  reflective veil over its holes. The user's DB_Track1 maps are fully rough
  there (about 1.6 % reflectance), so nothing shows yet. To fix: a way to
  tell coverage (everything fades with alpha) from glass (2026-10-27).

### Occlusion (the user's question, 2026-10-27)

- The texture's occlusion and the view's GTAO multiply: the material
  darkens its environment light by its map, GTAO subtracts its share of
  what is left. They mostly see different things (painted creases and
  rivets; geometry at screen scale) and overlap where a bake holds large
  shapes too. A material made PBR in the editor starts at occlusion
  strength 0.6 against that (the `.sd`'s default stays 1). Taking the
  smaller of the two (Filament) needs the baked value as one more output of
  every lit shader for the GTAO pass: not now.
- Reflections: specular and horizon occlusion (shapes task 06, Occlusion)
  instead of the diffuse occlusion.

### Track profiles (the user, 2026-10-27)

Procedural profiles (`TrProfile` LODItems) draw as S files do:
- `LightModelName` is the MSTS light material Open Rails reads it as
  (`Viewer3D/Common/Helpers.cs`): `OptSpecular25` and `OptSpecular750` give
  the -6/-7 specular. One function (`MstsMaterials::applyLight`) now lights
  S files (both backends) and profile objects.
- PBR LODItems: `ShaderName ( "PbrTexDiff" )` or `BlendAPbrTexDiff`,
  `TSREOrmTexName`, `TSRENormalTexName`, `ESD_TSREMaterial` (the .sd line's
  values without the key). `TexDiff`/`BlendATexDiff` with all three tokens
  are upgraded, with a diagnostic, so Open Rails still draws such a profile
  (the user: no other hacks; Open Rails may take the shaders in a PR). The
  meshes go into the PBR layout with generated tangents and the packets
  get `MstsMaterials::fill`. Mapping table and syntax: tracks task 07.

## Steps

1. `Kind::ShapePart` and per-part picking in the Shape Viewer and the
   consist preview; the picked part highlighted.
2. MSTS specular (-6, -7), with captures.
3. PBR S materials: the shader names, three textures, the `.sd` factors,
   generated tangents, into `RenderItem::Pbr`; both MSTS backends, both
   renderers.
4. The materials editor: window, assigning, saving, undo.
5. Tests: S round trip with PBR states (shape-complex), the `.sd` line,
   selection ids, Shape Viewer captures.
