- some gltf / glb assets with lighting, street lamps, railways lamps etc. anything useful for route buildong.
- trprofile templates using both 2d and/or 3d mesh specs:
-- tracks, route PROCEDURAL has some examples, modern rail profile in obj, prepare some tracks with this new rail, different ballasts, try 2.5D approach like DB1 does, try 3D, make LODs, different ties, bridges, etc.
-- roads, sidewalks, try to follow Poland's infrastructure specs
-- fences, wooden, brick, try 3d profile poles,
-- other ideas
- don't put them in TSRE repo / workdirs. Make new dir, categorize in dirs by type / usage. Keep template with it's textures for easy copy / pasete
## Checking assets

Use the TSRE build in `/home/arch/NetBeansProjects/TSRE5vc-profile-capture`: run it from there, but don't edit or commit anything in it. Its `docs/features/trprofile-capture.md` describes the capture spec; `tests/renderer/profile-views.json` there is a working example on the PROCEDURAL route.

```bash
cd /home/arch/NetBeansProjects/TSRE5vc-profile-capture
xvfb-run -a -s "-screen 0 1920x1080x24" build/TSRE5vc --game-root /home/arch/ORTS/MSTS \
    --test --test-suite=shape-viewer-capture --test-cases <your spec>.json --test-label <label>
```

- Track profiles: lay each one out like a route (`<asset>/TRACKPROFILES/name.stf`, `<asset>/TRACKPROFILES/meshes/`, `<asset>/TEXTURES/`) so it renders where it is and copies into a route as it is. In the spec, use an absolute `itemRoot` and `output` outside the TSRE dirs.
- Capture every profile on the `straight` and `curve` paths, plus one close-up (`zoom`, `yaw`, `pitch`). `diagnostics` and `missingTextures` in `capture.json` must be empty; look at the images too.
- glTF / glb assets: the same capture with `"type": "shape"` items (`yaw`, `pitch`, `zoom` work there too).
- Junctions and anything else that needs a track database can't be checked this way; list them for a check on a route.
