# Task 03 - Complex Shape Metadata Sidecar (TSRE JSON)

## Objective
Provide a TSRE-native way to store **engine/editor metadata** for complex shapes (snapables, overrides, extra hints) that is **independent from the source model format** (glTF/GLB, future FBX, etc.).

This complements MSTS `.sd` (which remains the metadata carrier for MSTS `.s`).

## Background / Motivation
MSTS shapes already have optional `.sd` files that TSRE uses for:
- editor-only features like `snapable`,
- detail-level defaults (`esdDetailLevel`),
- optional complex bounds (`esdBoundingBox`),
- seasonal texture path adjustments.

When we add new complex shape formats (glTF/GLB, etc.), we still need a place to store TSRE-specific metadata that:
- should not require editing the source model file,
- can be written by TSRE tools/editors,
- can evolve without being constrained by a third-party format.

## Key Decision
- Keep MSTS `.sd` only for MSTS `.s` (existing ecosystem behavior).
- Add a UTF‑8 JSON sidecar for **non-MSTS** complex shape formats.

Optional future enhancement:
- for glTF/GLB we may also read/write metadata from `extras` / a TSRE extension for single-file packaging, but sidecar remains the preferred editable override.

## Proposed Sidecar Filename
For a model file `pathid`:
- sidecar path: `pathid + ".tsre.json"`
  - example: `model.glb.tsre.json`
  - example: `model.gltf.tsre.json`

This keeps metadata unique per asset file, even if multiple formats share the same base name.

## Scope (v1)
- Define a minimal metadata schema and versioning.
- Implement a loader that reads the JSON file (if present) and provides metadata to a `ComplexShape` implementation.
- Integrate with the `ComplexShape` contract so the editor/world can consume metadata through existing methods:
  - `isSnapable()`, `addSnapablePoints(...)`
  - `getFloorBorderLinePoints(...)` (optional)
  - `getEsdDetailLevel()`
  - optional bounds overrides (`getBound()/getBoxPoints()`) if we decide to support them
- Ensure missing/invalid sidecar fails gracefully (log once, use defaults).

## Out of Scope
- Migrating MSTS `.sd` to JSON.
- Automatic metadata generation (beyond basic “compute bounds” already done by formats).
- Global metadata databases or per-route overrides.

## Suggested JSON Schema (Draft)
Top-level object with a version number:
```json
{
  "tsreMetaVersion": 1,
  "snapable": false,
  "snapPoints": [
    [ -1.0, 0.0, 0.0 ],
    [  1.0, 0.0, 0.0 ]
  ],
  "defaultDetailLevel": -1,
  "boundsOverride": {
    "min": [ -1.0, 0.0, -1.0 ],
    "max": [  1.0, 2.0,  1.0 ]
  }
}
```

Notes:
- `snapPoints` are in **shape local space**.
- `defaultDetailLevel` maps to `ComplexShape::getEsdDetailLevel()` semantics (negative means “no hint”).
- `boundsOverride` is optional; if present, it overrides computed bounds (useful when imported geometry has wrong bounds or when we want editor-specific bounds behavior).

## Integration Approach (Implementation Sketch)
- Create a small helper (e.g. `ComplexShapeMeta`) and JSON parser using Qt (`QJsonDocument`).
- Each non-MSTS `ComplexShape` implementation (glTF/GLB, etc.) loads metadata after parsing the model file (or lazily on first query).
- Cache “metadata missing” state to avoid repeated filesystem probes.

## Acceptance Criteria
- glTF/GLB shapes can be marked snapable via sidecar JSON without changing the model file.
- Tools that currently rely on `.sd`-backed behavior (snapable endpoints, default detail level) can work for non-MSTS shapes when JSON is present.
- Missing/invalid JSON does not prevent shapes from loading or rendering.

