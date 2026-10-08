# Task 02 - Trees and Bushes

Status: ready; first batch not started.

## Objective

Generate 3D trees and bushes for TSRE and Open Rails routes, starting with
the species most common in Poland. Later the same generator should cover
other foliage (hedges, reeds, tall grass, scrub along the track).

Trees come from a generator, not from hand modelling. This task is about
shape and texture generation only. Engine integration is a later task,
started only if the first batch looks promising. Three ways are open:

- a generated shape placed as an ordinary static object;
- a custom vegetation object built on top of the generated assets;
- an in-game generator, as TrProfiles are.

The dedicated vegetation object is the likely choice, rather than generic
object placement.

A small foliage generator for terrain patch materials is also planned, so
the generator must be portable: it must fit any of the three ways and share
code with that foliage generator (see "Runtime or cached").

Work in batches, as in `01-assets.md`. **Stop after the first batch** and
report: the point of the first batch is to find out whether the generator
and the LOD approach give a usable look.

## What a tree is

This is a train simulator: trees are seen from a moving cab, mostly at
20 m and more, often in hundreds. The detailed shape must stay cheap.

- **Trunk and main branches**: tapered tubes, a few levels of branching,
  low side count that drops with branch thickness.
- **Foliage**: no individual leaves. Small twigs and leaves are textured
  cards (alpha-masked quads showing a leafy branch or a needle spray),
  attached along the outer branches. Cards are double-sided.
- **Variants**: each species gets several variants from different seeds,
  and at least two sizes (young, mature), so a row of trees doesn't look
  cloned.
- **Pivot and scale**: origin at the base of the trunk, on the ground, +Y up,
  metres. Real heights for the species (listed in its README).

## LODs

Cut-offs are given as a multiple of the tree's height H, so a 4 m bush
switches much closer than a 25 m oak. The proposed values assume a 1080p
cab view with a 45° vertical field of view (about 1300/d pixels per metre
at distance d):

| LOD | Content | Triangles | Used up to | 20 m tree | Tree on screen at the cut |
|---|---|---|---|---|---|
| 0 - detailed | trunk and branch tubes, foliage cards | 2-5k (bush 0.5-1.5k) | 3 H | 60 m | ~430 px |
| 1 - reduced | trunk and main limbs, fewer, larger cards | 300-800 | 10 H | 200 m | ~130 px |
| 2 - flat | cruciform: 2-3 crossed vertical quads (later a camera-facing billboard) | 4-12 | beyond | | |

- **Forest budget**: a line through a dense forest (400 trees/ha, trees in
  about a fifth of the circle around the camera) gives roughly:

  | Distance | Trees in view | LOD | Triangles |
  |---|---|---|---|
  | 0-60 m | ~70-90 | 0 | ~0.3M |
  | 60-200 m | ~900 | 1 | ~0.5M |
  | 200-1500 m | ~55,000 | flat | ~0.45M |

  About 1.3M triangles in all, which a GPU handles. Without LOD 1, LOD 0 would
  run to 200 m (about 1000 trees, 4M triangles) or the flat LOD would start at
  60 m, where the cruciform is easy to see. So LOD 1 is expected for forests;
  the first batch confirms it in the captures. Single trees and tree rows can
  keep LOD 0 further out.
- Triangles are not the real limit. The limits are draws and alpha overdraw:
  tens of thousands of flat trees need instancing (the TSRE renderer has it)
  or merged meshes, as MSTS `forest` objects do, and stacked foliage cards
  cost fill rate. These are engine integration questions; the report notes
  what the captures show. Keep LODs friendly to both: one material per LOD
  where possible (bark and foliage in one atlas for LOD 1 and the flat LOD),
  so an instance or a merged batch is one draw.
- The flat LOD's textures are rendered from LOD 0 by the generator (side
  views from 2-3 directions, alpha), so it matches the 3D tree. A camera-facing
  billboard is preferred over the cruciform, but the engine doesn't support it
  yet; the cruciform needs nothing new.
- **Engine gap**: TSRE's glTF loader has no distance levels yet. It will need
  them anyway: Open Rails glTF support (PR #570, "glTF 2.0 support with PBR
  lighting", in Open Rails unstable) is reviewed for how it does distance
  levels at the integration stage, not in this task. Until then each LOD is a
  separate file (`<tree>_lod0.glb`, `<tree>_lod1.glb`, `<tree>_flat.glb`), and
  `<tree>.lod.json` next to them lists the files and cut-offs (as a multiple of
  H and as screen height in pixels), so whichever integration is chosen can
  read them. The LOD switch is checked by capturing the levels side by side
  at their cut-off distances.

## Textures

Realistic look; procedural is fine, as is a photographic source with a clear
licence (same rules as `01-assets.md`, "Textures and licences").

- bark: a tileable texture per species (base colour, normal map), mapped
  along the trunk without visible stretching;
- foliage: one atlas per species holding several leaf-spray or needle-spray
  cards, alpha mask (`alphaMode: MASK`, which TSRE supports), some colour
  variation between cards;
- flat LOD: one atlas per variant, rendered from LOD 0.
- Sizes (proposed): bark 512x512, foliage atlas 1024x1024, flat LOD 512x1024.
- Summer only in this task. The generator still writes every texture as its
  own PNG first and packs it into the glb last, so seasons can be added
  without changing the generator. How glTF gets seasons is decided with the
  engine integration. TSRE's seasonal directories work only for MSTS `.s`
  shapes with an `.sd` today
  (`docs/tasks/shapes/09-shape-seasonal-textures.md`). Options:
  - glTF with external textures (`.gltf` or glb with image URIs), the engine
    looking them up in seasonal directories as it does for MSTS shapes;
  - `KHR_materials_variants`: one glb holds a material set per season, the
    engine picks one by name; self-contained and standard;
  - one file per season.

  A bare winter deciduous tree is different geometry (no foliage cards), so
  it is a separate variant whatever the choice.

## Runtime or cached

This task makes **cached shape sets**: the generator runs offline, the glb
files are committed. To keep a runtime generator possible later:

- the generator is deterministic: species parameters + seed + size give the
  same tree every time;
- species are data (one parameter file each), not code;
- geometry generation uses only plain Python and numpy, so it can be ported
  to C++ without a modelling library;
- the core (branching, cards, LOD reduction, texture synthesis) works on
  plain arrays and does no file I/O. The writers (glb, OBJ, `.ace`, sidecar
  JSON) are separate, so an in-game generator or the terrain foliage
  generator can reuse the core and the textures without the files.

A parametric branching model (Weber-Penn style, as used by Blender's Sapling)
is the proposed starting point; space colonisation can be tried for species
where it looks wrong.

## Species - first batch

Most common trees in Poland, plus the bushes seen along Polish lines:

| Species | Polish name | Kind | Notes |
|---|---|---|---|
| Scots pine | sosna zwyczajna | conifer | most common forest tree; tall bare trunk, flat crown |
| Norway spruce | świerk pospolity | conifer | conical, drooping branches |
| Silver birch | brzoza brodawkowata | deciduous | white bark, hanging twigs; typical on embankments |
| Pedunculate oak | dąb szypułkowy | deciduous | broad crown, field and roadside tree |
| Black locust | robinia akacjowa | deciduous / bush | very common along railway lines |
| Common hazel | leszczyna pospolita | bush | multi-stem bush |
| Black elder | bez czarny | bush | multi-stem bush, scrub and edges |

Later batches: beech, black alder, linden, Norway maple, poplar, pollarded
willow, ash, field hedges, young self-sown pine and birch scrub, winter
(bare) variants.

## Project

The generator lives in `/home/arch/NetBeansProjects/tree-gen`, its own
local git repository (create it at the start), outside every TSRE checkout.
Nothing goes into the TSRE repositories or the MSTS routes.

```text
tree-gen/
|-- README.md                  how to run, species list with previews
|-- treegen/                   the generator (geometry, cards, LODs, glb writer)
|-- species/<species>.json     species parameters
|-- textures/                  texture generators and sources
|-- checks/                    capture specs
|-- build/                     capture output (git-ignored)
|-- reports/                   one report per batch
`-- out/<species>/             generated assets, laid out like 01-assets.md
```

Each `out/<species>/` follows the asset layout of `01-assets.md`
(`SHAPES/`, `source/` pointer, `preview.png`, `README.md`,
`THIRD_PARTY.md` when needed). The trees stay in `tree-gen` for now; moving
them into the `assets-gen` library is decided with the engine integration.

## Tools

As in `01-assets.md`: shared production server, no system package installs,
ask before any per-user install. python3 with numpy, ImageMagick, xvfb-run,
the TSRE capture build. No Blender.

- Outputs, for every variant:
  - **glb** with embedded PNG textures, one file per LOD (the main output);
  - **OBJ** + MTL per LOD, textures as PNG next to them (`map_Kd`, `map_d`
    for the foliage alpha, `map_Bump` for normals), for other tools. OBJ has
    no alpha mode and no LODs, so the glb is the reference;
  - **forest texture**: a front view of the tree with alpha, rendered like
    the flat LOD and converted to `.ace` for MSTS `forest` objects
    (`TSRE5vc --aceconv --file x.png --output x.ace --ace-format dxt5 --mipmaps`).
- The glb writer may start from
  `TSRE5vc-profile-capture/tests/renderer/lights/make_street_lamp.py`.
- Rendering the flat LOD atlases needs an offscreen renderer: either a small
  numpy rasteriser in the generator or orthographic TSRE captures. Pick the
  simpler one in the first batch and say which in the report.

## Checking

Same tools and rules as `01-assets.md`, "Checking assets" (glTF section,
and "This machine"):

- every LOD of every variant in `shape-viewer-capture`: a close-up, a
  medium view (about 30 m) and a far view (about 150 m), from the side and
  slightly from above;
- no `glTF:` errors or texture warnings in the log, `settled` true;
- look at the images: alpha edges (no dark or white halos), cards seen
  edge-on, back faces, bark stretching, scale next to a reference object
  (a lamp from `01-assets.md` or a 2 m box);
- each pair of neighbouring LODs side by side at their cut-off distance:
  do they match in size, colour and silhouette;
- a group of 50-100 trees in a route scene (`renderer-capture` with
  `objects` on `TEST_PROFILES`, linked root as in `01-assets.md`), LODs
  placed by distance by hand, to see repetition and the forest edge. This
  machine renders on llvmpipe, so frame times say little: report triangle
  and object counts for the scene instead.

## Report and stop

After the first batch, write `tree-gen/reports/01-first-batch.md`:

- each species: variants, triangle counts per LOD, texture sizes, preview;
- a contact sheet of all captures;
- whether LOD 1 is needed, and the cut-offs after looking at the captures;
- the forest budget table above, redone with the real triangle counts;
- what the engine needs for proper tree LODs and billboards (glTF distance levels,
  camera-facing billboards, wind), as input for a TSRE task;
- what worked and what didn't in the generator; time per species;
- proposed changes to this task.

Then stop and wait for review.

## Acceptance (first batch)

- The seven first-batch species exist, each with at least 3 variants, LOD 0,
  LOD 1 and the flat LOD (glb and OBJ), a `.lod.json`, a forest `.ace`, a
  README and a preview.
- Generating them again from the committed parameters and seeds gives the
  same files.
- Every glb loads without `glTF:` errors and passes the capture checks.
- Triangle counts are within the budgets above, or the report says why not.
- The report is written.

## Decided (2026-10-08)

- Shape and texture generation first; engine integration (static object,
  vegetation object or in-game generator) is a later task, if the results
  look promising.
- Cached shape sets; generator deterministic, numpy only, species as data,
  core shared with the planned terrain foliage generator.
- One glb per LOD until TSRE has glTF LODs, plus a `.lod.json`.
- Outputs: glb, OBJ, forest `.ace` textures. MSTS `.s` stays possible later
  but is not part of this generic step.
- First batch: the seven species above, summer only.
- The triangle budgets and the 3 H and 10 H cut-offs are the starting point;
  rethink them if the first batch shows practical problems.
- Distance levels, seasons and instancing or merging are decided at the
  integration stage.
- Assets stay in `tree-gen`.
