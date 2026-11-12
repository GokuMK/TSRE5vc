# Task 02 - Skinned glTF in TSRE

Status 2026-11-12: built on `feature/humans` (worktree `../TSRE5vc-humans`),
steps 1 to 5; what was built and measured is in "Results" at the end. Not
merged.

## Goal
glTF skins and animations loaded, played and drawn in both renderers, and
the Shape Viewer playing, blending and showing clips. This is the base for
crowds (task 03) and later for the player's humans.

## Test set
Khronos samples already on this box (`../glTF-Sample-Assets/Models`, the
path in `TSRE_GLTF_SAMPLE_ASSETS`):

| Sample | Why |
|---|---|
| SimpleSkin, RiggedSimple | Smallest cases |
| RiggedFigure, CesiumMan | A walking figure |
| Fox | Three clips: crossfades |
| BrainStem | Many joints and meshes |
| RecursiveSkeletons | Odd hierarchies |
| InterpolationTest | STEP, LINEAR and CUBICSPLINE |
| AnimatedCube, BoxAnimated | Rigid node animation (in this task, the user 2026-11-11) |

Plus the example humans from task 01 (a man, a woman, a young person).

## Reading (`GltfShape`)
- **Skins**: joints (node indices), inverse bind matrices, skeleton root.
  The meshes' `JOINTS_0` (unsigned byte or short) and `WEIGHTS_0` (float,
  or normalised byte or short).
- **Animations**: channels on nodes' translation, rotation and scale;
  samplers LINEAR, STEP and CUBICSPLINE. Morph target weights are left out,
  with a message.
- The joint hierarchy comes from the node tree.
- **Rigid animation** (decided): nodes that are animated but belong to no
  skin. Proposal: ozz samples them like a skeleton (the nodes as joints),
  and their matrices go to the parts through the existing pose path
  (`setPartTransform`). One sampler for both kinds.
- Messages for:
  - more than 4 influences (`JOINTS_1`),
  - a mesh skinned by two skins,
  - missing inverse bind matrices, read as identity, as the glTF
    specification says.
- Clips from the shared clips file (named in the route's `humans.dat`) are
  bound to a human by joint names. The human's own clips, if any, come
  first.
- **Playing a shared clip on a human** (task 01 made the joint frames the
  same on every human, 2026-11-12): the clip's rotations as they are; the
  human's own joint translations (its bone lengths); the pelvis's
  translation from the clip, minus the clip skeleton's pelvis rest, times
  the human's pelvis height over the clip skeleton's, plus the human's
  pelvis rest. Other translation and scale channels, if a file has them,
  are ignored for shared clips. No retargeting in TSRE.
- **Levels of a human** (task 01's files, 2026-11-12): `MSFT_lod` on the
  level 0 mesh node, the scene's first root (Open Rails' rule), the
  skeleton the second root; the lower levels outside the scene, all skinned
  by the same skin. TSRE's group rules (task shapes 10) take a group
  anywhere. The files carry tangents (MikkTSpace).
  Each level is a skinned mesh; the pose is sampled once per person and
  drawn with whichever level is chosen.
- **A walk's speed** is measured at load: the libraries' walks are in
  place, so it is the speed of the foot on the ground sliding back (the
  generator's `walking_speed` does the same in Blender).

## ozz in TSRE
- Its sources in `thirdparty/ozz-animation` (0.16.0), built by TSRE's
  CMake like the other libraries there (TSRE's rule: no external
  libraries). Only its base, runtime and offline builder sources: no tools,
  samples or importers. MIT notice in `thirdparty/README.md`.
- **At load**, on the shape-loading worker:
  - glTF goes into ozz's `RawSkeleton` (joint names, rest transforms),
    built by `SkeletonBuilder`.
  - Each clip goes into a `RawAnimation` (keys per joint; CUBICSPLINE
    converted to sampled keys), built by `AnimationBuilder`. It may be
    reduced with ozz's optimiser at a small tolerance.
  - The results are shared by every state of the shape.
- **Per state** (one drawn human), it keeps:
  - its sampling contexts,
  - the clips playing (name, time, weight, speed),
  - its blended local pose,
  - its model matrices,
  - its skinning palette (model matrix × inverse bind matrix).
- ozz orders joints depth-first. A map keeps glTF's joint order for the
  palette.

## The API
Proposal: extend `ComplexShape`'s state API, as rigid poses did:

- `clipCount()`, `clipName(i)`, `clipDuration(i)`
- `setClips(stateId, {clip, time, weight}...)`: the caller (the Shape
  Viewer, the crowd) owns the time.
- `updatePose(stateId)` samples and builds the palette. The crowd (task 03)
  calls it for many states at once on a QThreadPool before the frame's
  gather. The Shape Viewer calls it once.
- `partMatrix` also for joints, to attach things to a hand later and for
  the player's camera.

## Drawing
- **Vertex layout**: PBR's 19 floats, plus 4 joint indices (16-bit) and 4
  weights. A new layout, `PbrSkinned`.
- **Palettes**: every skinned state drawn in a frame writes its palette
  into one RGBA32F data texture (3 texels per joint, a 3×4 matrix). An
  instance passes its offset into that texture, as instancing passes its
  matrices today. This works on OpenGL 3.3 and on QRhi. A large uniform
  buffer would not suit, since QRhi on OpenGL emulates uniform buffers with
  `glUniform` calls.
- **Shaders**: skinned versions of the PBR program, the shadow pass and the
  selection pass, in both renderers.
- **Bounds**: at load, each clip is sampled and the union of its joint
  positions, widened by the mesh's radius, gives a box that culling can use
  while the human moves.
- **Instancing**: skinned instances of one mesh level batch as rigid ones
  do. Only their palette offsets differ.
- **Later**: motion vectors (task renderer 38) need the previous frame's
  palette too, from a double-buffered palette texture.

## Shape Viewer
- The Animation menu (it exists for MSTS keys) lists glTF clips: play,
  pause, speed, loop, scrub.
- A clips file opened from the menu (the viewer has no route
  configuration), its clips listed beside the human's own.
- A second clip with a blend weight, to test crossfades.
- Walking in place, or moving forward with the clip's measured speed.
- Show the skeleton (lines between joints). Force a level of detail.
- `shape-viewer-capture` takes a clip name and a time, so frames repeat.
  Both renderers.

## Steps
1. **Reader**: skins, animations, messages. A suite (`gltf-skin`) reads
   the Khronos samples and checks joint counts, clip lengths and key
   counts.
2. **ozz**: vendored and built on Linux and on Windows (MinGW). Raw
   skeletons and animations from the reader. The suite compares ozz's
   sampled poses of CesiumMan and Fox at given times with a plain glTF
   evaluation written in the test.
3. **Skinning**: the OpenGL renderer first, then QRhi: layout, palette
   texture, the three shaders, bounds.
4. **Shape Viewer**: the controls; captures of the samples and the example
   human at fixed times in both renderers, compared with task 01's Blender
   renders.
5. **Measure** sampling and palette time per character in a Release build
   (a test executable, 1, 100 and 500 characters). The result decides how
   much task 03's update levels must save.

## Acceptance
- Khronos' skinned and rigid animated samples animate as in reference
  viewers, in both renderers. The user checks the captures.
- The example humans play `idle` and `walk` and crossfade between them in
  the Shape Viewer.
- The measured cost per character is recorded here.

## Not in this task
Morph targets (faces), IK, crowds and impostors (task 03), physics.

## Results (2026-11-12)

### What was built
- **ozz-animation 0.16.0** in `thirdparty/ozz-animation` (base, runtime and
  offline builders; README with the archive's SHA-256), the `tsre_ozz`
  library, its MIT notice in the About window.
- **`SkeletalAnimation`** (`src/tsre/shape/SkeletalAnimation.h/.cpp`): a
  file's node tree as one ozz skeleton, every node a joint (rigid node
  animation and skins share one sampler; up to ozz's 1024 nodes), glTF
  animations as ozz clips.
  - A node a clip does not key keeps its rest transform (ozz would take
    identity).
  - STEP becomes pairs of keys, CUBICSPLINE is sampled at 60 per second.
  - Rotation keys more than 10° apart get slerp keys between them: ozz
    interpolates normalized-linearly, glTF wants slerp. Without that,
    AnimatedCube and BoxAnimated were off by 0.09-0.10 between keys.
  - `addClips` binds another file's clips by node names, as task 01
    decided: rotations as they are, translations moved from the clip
    skeleton's rest to this one's and scaled by the ratio of the node's rest
    heights, scale left out; a name already present is skipped.
- **`GltfShape`**: skins, animations, `JOINTS_0`/`WEIGHTS_0` read (a second
  set is left out with a message); skinned primitives in a new layout,
  `RenderItem::PBR_SKINNED` (PBR's 19 floats, then 4 joint indices as floats
  and 4 weights, 27 floats).
  - Only a file with animations or skins gets a skeleton; every other glTF
    shape draws exactly as before (shared packets).
  - An animated state gets its own copies of the level's packets (as
    `E3dShape` does): rigid nodes point at the pose's node matrices; skinned
    primitives draw in the skeleton's space (identity model matrix, as glTF
    ignores a skinned mesh node's transform) with the pose's joint rows.
  - Bounds each push: a skinned primitive's sphere is around its skin's
    joints as posed, widened by the farthest any vertex lies from a joint
    moving it (measured at load per skin). The shape's own bounds (camera
    fit, screen coverage) use the rest pose the same way.
  - One mesh may serve many skins (RecursiveSkeletons: one mesh, 84 skins).
  - The copies follow the shared set by a build counter: a rebuilt set's
    packets can reuse the old ones' addresses, and the copies then kept
    showing the textures as missing (magenta) after they had loaded.
  - API (`ComplexShape`): `clipCount`, `clipName`, `clipDuration`,
    `setClips(state, {clip, time, weight}...)`; `GltfShape::updatePose`,
    `addClipsFrom`, `skeleton`, `poseNodeMatrix`, `poseJointRows`.
- **Drawing**: joints and weights at attribute locations 7 and 13; the joint
  matrices as a per-draw uniform array (`jointRows`, 3 `vec4` a joint, 64
  joints at most, `appdata/0.7/shaders330/Skinning.glsl`), not the data
  texture the design proposed. Every QRhi draw already has its own uniform
  block, and animated states draw their own packets anyway (no instancing to
  lose), so no frame-level texture with its upload-ordering pitfalls was
  needed. Skinned programs in both renderers: PBR, selection, the sun's
  shadow maps and (QRhi) a lamp's; the OpenGL shadow pass switches program
  per packet now. Skinned packets cast shadows (`castsShadow` takes the
  layout) and write no velocity (motion blur treats them as still).
- **Shape Viewer**: Animation menu: Clip (the first is chosen once a shape
  with clips loads; Rest Pose), Blend With and Blend Weight (25/50/75 %, at
  the same share of its length), Speed (0.25-2x), Load Clips File (its clips
  added by joint names as "walk (people_clips)"). Play advances the time.
- **Captures**: `shape-viewer-capture` items take `clip`, `clipTime`,
  `blend`, `blendWeight`, `clipsFile`. Case files
  `tests/renderer/gltf-skin-views.json` (samples) and
  `tests/renderer/humans-views.json` (`TSRE_HUMANS_SHAPES`: humans-gen's
  `out/route/SHAPES`).
- **Suite `gltf-skin`** (`TSRE_GLTF_SAMPLE_ASSETS`, optionally
  `TSRE_HUMANS_SHAPES`): every sample's node matrices at the start, between
  keys and at the end of every clip, and its skins' joint matrices, against
  an evaluation written in the test from the specification (slerp, STEP,
  Hermite); the packets two states submit; a file without animations keeps
  no skeleton; the humans' skins, clips and levels; the shared clips file on
  the woman against her own clips; the cost per character (printed).

### Checks
- `gltf-skin` 86 checks pass. All samples within 0.0007 (of the model's
  size) of the specification, including InterpolationTest's three kinds;
  the shared clips file on the woman within 0.00017 of her own.
- Captures in OpenGL and QRhi Vulkan (lavapipe): CesiumMan, Fox (Walk, Run,
  both blended), RiggedFigure, BrainStem, RecursiveSkeletons, BoxAnimated,
  and the three humans with their clips, blends and the shared file.
- Suites gltf-lod, gltf-pbr-gl, rhi-shaders, shape-animation, shape-lights,
  shape-emitters, mesh-store, local-lights, eng-lights, displays,
  shape-complex-gl, token-shape-gl, e3d-shape, shape-materials,
  terrain-mesh-gl, transfer-mesh pass; every target builds; ctest 11/11.
- No change for other glTF shapes: `tests/renderer/gltf-viewer-views.json`
  (33 samples) captured with this branch and with its base (`d1f82b12`,
  built apart), in OpenGL and QRhi Vulkan: 32 pixel for pixel the same,
  CommercialRefrigerator (glass) within RMSE 0.01-0.02, no pixel over the
  tolerance.

### Cost (Release, this machine, one core)
The man (53 joints) playing `walk`: sampling with its joint matrices 6-9 µs
a character; the whole push (sampling, posed packets, bounds) about 7 µs a
character: **3.4 ms a frame for 500 characters**. Task 03's target was 1 ms:
shared poses, update tiers and threads are needed there. GPU time is for
the user's hardware.

### Open
- Skinned meshes write no velocity: motion blur and TAA see them as still
  (needs last frame's joint rows).
- Joints as parts (`partCount`/`partMatrix`, for attaching a bag or the
  player's camera): left out, as it would change how `CarAnimation` treats
  glTF rolling stock.
- A skeleton display in the Shape Viewer.
- Skins over 64 joints are drawn unanimated, with a message.
- glTF shapes get no mipmaps unless they have an impostor (trees): humans
  will shimmer at a distance. Proposal: skinned shapes get mipmaps as trees
  do (task 03, with the impostors).
- Found, not this task: `GltfShape::adopt` does not take the light schedule
  (`schedule`, task renderer 46) from the worker's copy, so a glTF shape
  loaded on a worker keeps the default schedule.
