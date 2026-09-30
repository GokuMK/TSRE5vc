# Template3D example package

Copy `TrackProfiles` into a test route and create or copy
`TEXTURES/example-3d.png`. The texture may be a simple opaque checkerboard for
geometry testing.

- `TrProfile_3D_Track.stf` demonstrates Sweep and baked Repeat.
- `TrProfile_3D_Ruler.stf` demonstrates Stretch and shared Place at authored
  Ruler nodes.

The OBJ files use only the supported positive-index triangular `v/vt/vn`
subset. They are intentionally simple enough to inspect by hand.

From the repository root, validate the package with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\extra\trprofile-3d\validate-trprofile-3d.ps1
```
