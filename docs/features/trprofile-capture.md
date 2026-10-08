# Track profile captures

The Shape Viewer capture (`shape-viewer-capture`) builds a track profile
(Open Rails TrProfile, `.stf` or `.xml`) along a short path and saves an
image of it, with everything the parser and generator reported. No route has
to be loaded or edited, so a profile can be checked after every change.

## Running

```bash
TSRE5vc --game-root <MSTS root> --test --test-suite=shape-viewer-capture \
    --test-cases tests/renderer/profile-views.json --test-label <label>
```

Images and `capture.json` go to `<output>/<label>/`. Run the capture under
`xvfb-run` (or with a display); `shape-viewer-compare` compares two labels of
the same spec (`--test-label` and `--test-baseline`).

`tests/renderer/profile-views.json` captures profiles of the PROCEDURAL
route: plain track, DB1, a road lane, a brick fence and catenary.

## Spec

```json
{
  "output": "build/profile-capture",
  "itemRoot": "ROUTES/PROCEDURAL/TRACKPROFILES",
  "width": 1000, "height": 600,
  "background": [0.72, 0.78, 0.84],
  "items": [
    {"name": "db1-close", "type": "profile", "path": "TrProfile_DB1.stf",
     "zoom": 3, "yaw": 30, "pitch": 30}
  ]
}
```

- `itemRoot`: the directory item paths are relative to; a relative one is
  under the game root, an absolute one anywhere. `$NAME/...` takes an
  environment variable.
- `background`: the viewer's background colour (0 to 1). Thin parts such as
  wires read better on a light one.
- Profile items (`"type": "profile"`):
  - `path`: the profile file.
  - `file`: the profile's name or id, for a file holding several (a road file
    with its lanes); the first one otherwise. The log lists the file's
    profiles.
  - `track`: the path, `straight` (two 10 m straights, the default), `curve`
    (10 m straight, then 20 m of 100 m radius) or `long` (100 m straight,
    then 100 m of 500 m radius, for seeing levels of detail from afar).
  - `sections`: a path of its own instead, `[[length, radius], ...]` in
    metres, radius 0 for a straight and negative to curve the other way.
  - `yaw`: degrees the profile is turned; 0 looks at its side.
  - `pitch`: degrees it is seen from above (25 by default).
  - `zoom`: how much closer than the whole profile in view (1).
  - `textures`: the route the textures come from, when not the one the file
    sits in (see below).
- Shape items take `yaw`, `pitch` and `zoom` too.

## Profiles outside a route

Laid out like a route, a profile can be captured where it is and copied into
a route as it is:

```
<asset>/TRACKPROFILES/name.stf
<asset>/TRACKPROFILES/meshes/...      3D template meshes (paths relative to the file)
<asset>/TEXTURES/...
```

Textures are looked up in the route's `TEXTURES`, then in the game's
`GLOBAL/TEXTURES` two levels up. The route is the parent of the profile's
`TRACKPROFILES` directory, or the profile's own directory when it is in
another one.

## What it reports

For each profile item the log and `capture.json` give:

- `diagnostics`: parser and generator messages (`parse:`, `profile:`,
  `build:`), including features the generator does not support yet;
- `missingTextures`: textures the parts use that did not load. A missing
  `.ace` is reported as the `.dds` the loader tried first; those parts draw
  magenta;
- `shown: false` when nothing was built (unknown profile, bad path), with the
  reason in `diagnostics`.

Each part chooses its level of detail by its distance to the camera, as on a
route. Track junctions and anything else that needs the track database are
not covered: check those on a route.
