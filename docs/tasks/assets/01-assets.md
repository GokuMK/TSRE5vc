# Task 01 - Route Building Asset Library

Status: ready; first batch not started.

## Objective

Build a library of ready-to-use route building assets for TSRE and Open
Rails: track, road and fence profiles (TrProfile) and glTF/glb objects such as
lamps. Each asset is a self-contained template that copies into a route as it
is, and each one is checked with TSRE's capture tools before it counts as done.

Work in batches. **Stop after the first batch** (below) and report: the
point of the first batch is to find out whether the workflow works.

## Library

The library lives in `/home/arch/NetBeansProjects/assets-gen`, its own local
git repository (create it at the start), outside every TSRE checkout and
worktree. Nothing goes into the TSRE repositories, their work dirs or the
MSTS routes.

```text
assets-gen/
|-- README.md                  catalogue: one line per asset, with its preview
|-- tools/                     shared generators (OBJ, glb, textures, STF helpers)
|-- checks/                    capture specs and check scripts
|-- build/                     capture output, link game roots (git-ignored)
|-- reports/                   one report per batch
|-- track/<asset>/
|-- road/<asset>/
|-- fence/<asset>/
`-- lights/<asset>/
```

Every asset directory is laid out like a route, so copying its route
directories into a route installs it:

```text
<category>/<asset>/
|-- TRACKPROFILES/<asset>.stf      profiles (meshes/ below it for Template3D)
|-- SHAPES/<asset>.glb             glTF objects (textures embedded)
|-- TEXTURES/...                   profile textures (.ace)
|-- source/                        the asset's own generator script and texture sources
|-- preview.png                    one representative capture
|-- README.md                      what it is, main dimensions, consumers, how to install
`-- THIRD_PARTY.md                 only when third-party files are reused
```

Category directories may be split further by usage (for example
`track/standard-gauge/`) once there are enough assets to need it.

## Tools

This is a shared production server: no system package installs, and ask
before any per-user install. Available: python3 with numpy, ImageMagick,
xvfb-run, and the TSRE build named under "Checking assets".

- Generate geometry and textures with scripts. They are the source of truth;
  generated files are committed next to them so a template copies without
  running anything. `TSRE5vc-profile-capture/tests/renderer/lights/make_street_lamp.py`
  shows a glTF lamp written from plain Python.
- Blender is not used for now. If an asset needs modelling that scripts
  cannot do sensibly, note it in the report instead.
- Profile textures are `.ace` (TSRE and Open Rails both load them). Make a PNG
  source, then convert it without a window:
  `TSRE5vc --aceconv --file x.png --output x.ace --ace-format dxt1|dxt5 --mipmaps`.
- glb files embed their textures (PNG or JPEG).

## Textures and licences

Make your own textures (procedural, photographic sources only with a clear
licence). Reusing third-party textures is allowed for now: list each reused
file in the asset's `THIRD_PARTY.md` with its file name, where it came from
(route, pack, URL) and its licence, or "unknown", so it can be replaced
later. The textures of the MSTS routes on this machine (for example the
PROCEDURAL route's `DB_*`, `NR_*`) count as third-party.

## Formats and consumers

- TrProfile format: `docs/tasks/tracks/12-trprofile-improvements.md`
  (families, `ObjectType`), `docs/tasks/tracks/13-trprofile-3d-mesh.md` and
  `extra/trprofile-3d/README.md` (Template3D, OBJ subset, generation modes).
  Examples: `/home/arch/ORTS/MSTS/ROUTES/PROCEDURAL/TRACKPROFILES` and
  `docs/examples/track-profiles/`.
- Template3D (3D mesh) profiles work in TSRE and in Open Rails unstable, where
  they await merge. Template3D is STF only: write STF, never XML.
- Track profiles: `ObjectType ( TRACK ... )`; roads: `ObjectType ( ROAD ... )`,
  as families with `MAIN`, `LEFT`, `MIDDLE` and `RIGHT` members where the
  sides differ; fences, walls and poles placed along a Ruler:
  `ObjectType ( STATIC ... )`.
- Two ways to build a track, both wanted:
  - 2.5D, as `TrProfile_DB1.stf` does: polylines only, many LODs, rails drawn
    as textured strips;
  - 3D: Template3D meshes for rails (`Sweep`) and sleepers (`Repeat`),
    polylines for ballast, with simpler LODs further away.
- Every profile has LODs with sensible `CutoffRadius` values: small parts
  (rail detail, sleepers, fence boards) drop out first.
- The modern rail mesh to start from is
  `PROCEDURAL/TRACKPROFILES/meshes/uic60.obj` (UIC60, now 60E1); adjust it
  in a copy if it looks wrong.

## Polish infrastructure

Make the assets look like Polish railways, roads and fences. This is a game,
not a survey tool: plausible sizes and a visual review are enough. No
standard has to be looked up or cited.

- track: standard gauge, 60E1 and 49E1 rails, concrete and wooden sleepers,
  a ballast bed of the usual shape;
- roads: lane, shoulder and sidewalk widths and kerbs as seen on Polish
  streets and rural roads;
- fences: typical Polish forms (wooden board and picket fences, brick walls
  with pillars, panel fences).

List the main dimensions in the asset's README so they can be adjusted later.

## First batch

| Category | Asset | Notes |
|---|---|---|
| track | 60E1 rail, concrete sleepers (PS-94 type), 3D | Template3D rails and sleepers, polyline ballast, LODs |
| track | the same track, 2.5D | DB1 approach, for comparison with the 3D one |
| track | 49E1 rail, wooden sleepers, 3D | older line; reuses the 3D track's structure |
| road | two-lane street with kerbs and sidewalks | ROAD family: MAIN, LEFT, MIDDLE, RIGHT |
| road | rural two-lane road with shoulders | ROAD family or SINGLE |
| fence | wooden board fence | STATIC; 3D posts (`Repeat`), boards as polylines or mesh |
| fence | brick wall with 3D pillars | STATIC; pillars as `Repeat` or `Place ( Nodes ... )` |
| lights | street lamp | glb; emissive lens with `KHR_materials_emissive_strength`, `KHR_lights_punctual` spot light |
| lights | platform lamp | glb; as above |
| lights | yard floodlight mast | glb; several spot lights |

Later batches (not now): more ballast and sleeper variants, bridges, level
crossings, sidewalk and kerb variants, catenary, signals, platform edges,
and any other ideas that come up.

## Checking assets

Use the TSRE build in `/home/arch/NetBeansProjects/TSRE5vc-profile-capture`
(branch `feature/profile-capture`): run it from there, but don't edit or
commit anything in it. Its `docs/features/trprofile-capture.md` describes the
capture spec; `tests/renderer/profile-views.json` there is a working example
on the PROCEDURAL route and `tests/renderer/gltf-viewer-views.json` one for
glTF shapes.

Specs go in `assets-gen/checks/`, with an absolute `itemRoot` and an
`output` under `assets-gen/build/`. Keep the log of every run:

```bash
cd /home/arch/NetBeansProjects/TSRE5vc-profile-capture
xvfb-run -a -s "-screen 0 1920x1080x24" build/TSRE5vc --game-root /home/arch/ORTS/MSTS \
    --test --test-suite=shape-viewer-capture \
    --test-cases /home/arch/NetBeansProjects/assets-gen/checks/<spec>.json \
    --test-label <label> 2>&1 | tee /home/arch/NetBeansProjects/assets-gen/build/<label>.log
```

### Profiles

- Capture every profile on the `straight` and `curve` paths, plus one
  close-up (`zoom`, `yaw`, `pitch`); profiles with several LODs also on the
  `long` path.
- Capture every family member (`file` selects it).
- In `capture.json`, `diagnostics` and `missingTextures` must be empty and
  `settled` true. Look at the images too: gaps, flipped faces, stretched
  textures, LOD popping.

### glTF / glb

- The same capture with `"type": "shape"` items (`yaw`, `pitch`, `zoom` work
  there too). `capture.json` has no `diagnostics` for shapes: check the log
  for lines starting with `glTF:` (load errors) and for texture warnings,
  check `settled`, and look at the images.
- Lamps also on the QRhi renderer, which draws local lights:
  `--set=core.rendering.backend=qrhi --set=core.rendering.rhiApi=vulkan`.
  With time of day disabled (the default), local lights shine at full
  strength, so the light should be visible in daylight.
- Lamps in a route scene: `renderer-capture` with the spec's `objects`
  option places them for the capture only. Follow
  `tests/renderer/lights/lamp-views.json` and `lamp-root.sh`, but write your
  own copies in `assets-gen/checks/`: the script builds a game root of links
  under `assets-gen/build/` whose route also finds the asset in `SHAPES`.
  Use the `TEST_PROFILES` route:

  ```bash
  build/TSRE5vc --game-root <linked root> --route TEST_PROFILES \
      --test --test-suite=renderer-capture --test-cases <spec>.json \
      --test-label <label> --set=core.rendering.backend=qrhi
  ```

  The light should form pools on the ground and lit walls. An evening capture
  (`--set=core.rendering.timeOfDay.enabled=true`, a winter date, about 18:00)
  is optional.

### On a route

Junctions, and anything else that needs a track database, can't be checked
in the Shape Viewer. Check them on `TEST_PROFILES` through a linked game root
like the one above, so the route itself stays unchanged. If a check needs
route edits, copy the affected route files into the linked root instead of
linking them; never edit the original route. List whatever could not be
checked this way in the report.

### This machine

- No GPU: Mesa llvmpipe (and lavapipe for Vulkan). Images are correct but
  slow to render; always use `xvfb-run` or `QT_QPA_PLATFORM=offscreen`.
- Other sessions build TSRE here. Check `/proc/loadavg` before a batch of
  captures and don't run heavy captures in parallel (threaded texture
  loading once produced a false diff).

## Report and stop

After the first batch, write `assets-gen/reports/01-first-batch.md`:

- each asset: what it is, its consumers, the checks run and their results,
  a link to its preview;
- a contact sheet of all captures (`magick montage`) in `reports/`;
- what in the workflow worked and what didn't: tools, format limits,
  capture gaps, time per asset;
- TSRE or Open Rails bugs found, with a capture or log excerpt. Report
  them; don't fix them (no edits in the TSRE dirs);
- proposed changes to this task before the next batch.

Then stop and wait for review.

## Acceptance (first batch)

- The ten first-batch assets exist in the layout above, each with a README,
  a preview and its generator.
- Every profile passes its captures: empty `diagnostics` and
  `missingTextures`, `settled`, images checked.
- Every glb loads without `glTF:` errors; the lamps show their light on QRhi
  in the Shape Viewer and in the route scene.
- Every reused third-party file is listed in a `THIRD_PARTY.md`.
- Copying an asset's route directories into a route installs it with no other
  steps (tried at least once on the linked `TEST_PROFILES` root).
- The report is written.
