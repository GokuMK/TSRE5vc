# Terrain-level procedural material container

Date: 2026-09-30. Status: implemented; application build and automated verification passed;
actual ORTS stable / MSTS visual verification remains a user follow-up.

## Compatibility issue and agreed solution

Extended testing found that ORTS stable rejects unknown `terrain_samples`
children. Its terrain-level binary reader skips unknown children instead;
warnings while skipping are acceptable. Move the procedural extension into:

```text
terrain
  terrain_samples             standard sample metadata
  terrain_shaders             ordinary baked-fallback materials
  terrain_patches             ordinary patch UVs/material references
  TSRETerrainMaterials        0x00061004
    TSRETerrainMaterialBuffer unchanged .pmap reference
    TSRETerrainMaterialMap    unchanged local-ID -> UiD pairs
    TSRETerrainBakedMaterials unchanged version-2 revision/season records
```

The new outer block has standard framing/label plus child blocks, no additional
version/count. Existing child IDs and payloads remain unchanged. Other standard
terrain children are unaffected. This fixes extension placement, not ORTS's
other terrain resolution/patch-count restrictions.

## Implementation

- The cold preservation codec recognizes the outer container and bounds its
  children, preserving their framed bytes and labels. It does not build a DOM
  of procedural data or change the hot patch model.
- TFile reads both locations through the same child-payload decoder. There are
  no old prototype numeric-ID aliases. Supporting the old parent is a small
  compatibility loop, not a second implementation.
- TFile save relocates recognized sample-level blocks and emits only the new
  location. Unchanged child bytes, labels and supported trailing data survive.
- Valid old-location metadata automatically enrolls editable loaded local-route
  tiles in the ordinary unsaved-content list. It does not write on load or require
  `autoFix`. If no actual terrain edit occurred, Save writes the descriptor only:
  no RAW rewrite, patch-bound refresh, map generation or texture bake. An edited
  tile still follows the full normal save path. Failed saves retain the pending
  repair; successful file commits clear it. Stream/network serialization is not
  proof of disk persistence and does not clear it. Remote clients do not
  automatically enqueue this server-side format repair as a local edit.
- Mixed definitions across both locations, duplicate containers and duplicate
  known fields are ambiguous and cannot be rewritten. Structurally truncated
  containers fail bounded parsing; invalid known payloads retain existing
  invalid-presence behavior.
- Unrelated unknown data remains preserved. Unknown future children still
  prevent unsafe shader renumbering; known procedural fields do not.
- Clearing all procedural metadata removes the empty generated container;
  unknown future children are not removed by that operation.
- Migration only changes descriptor metadata. It does not touch `.pmap` data,
  baked images, shader/patch data, material UiDs or revisions. Normal route-save
  work for genuinely edited tiles still runs independently. No user route files
  are modified by tests.
- Static descriptors have no newly generated extension container. Older TSRE
  versions will not recognize the new placement and use static fallback.

## Verification

Targeted tests cover both placements, canonical new-location saving, exact child
bytes, labels/tails, material IDs, seasonal revisions, absence of old copies,
future-child preservation, shader-remapping guards, conflicting metadata,
malformed framing and removal when procedural metadata is cleared. Existing
token tests cover static byte compatibility and invalid extension payloads.

Standalone results: `simis_tokens` 1,631 passed / 0 failed; `terrain_tfile`
105 passed / 0 failed. Both compile and exercise production TFile/codec sources.
The rebuilt application's `terrain-material` suite passed 582 / failed 0,
including pending-save enrollment, write-disable, descriptor-only save with
unchanged RAW data and reloading without a repeated repair flag. The application
and targeted standalone builds used at most two compilation jobs.

Run the standalone `simis_tokens` and `terrain_tfile` suites, plus app
`terrain-material` after rebuilding with at most two jobs. Then load a saved
procedural tile in unmodified ORTS stable and MSTS: neither should parse the
extension children; standard baked fallback should still render. A warning is
not a failed compatibility result. No claim of an actual ORTS/MSTS run is made
by TSRE's own codec tests.

References: [native token format](../../features/native-token-ids.md),
[material library](../../features/terrain-material-library.md),
[seasons](../../features/terrain-procedural-seasons.md),
[ORTS terrain reader](https://github.com/openrails/openrails/blob/master/Source/Orts.Formats.Msts/TerrainFile.cs),
[ORTS binary block reader](https://github.com/openrails/openrails/blob/master/Source/Orts.Parsers.Msts/SBR.cs).
