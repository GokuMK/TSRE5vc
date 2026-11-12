# Task 02 - Skinned glTF in TSRE

Status: design, 2026-11-11.

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
