# Task 11 - Baked Procedural Track Sweep

## Objective

Replace the temporary road-lane rigid-plane compromise with migration-safe
procedural rendering. Every generated companion must persist its real
equal-height endpoint in RDB. Visual continuity must come from the profile
sweep, not from moving an inner or outer lane endpoint onto the main object's
tilted local plane. Apply the same rendering model to template-generated
`DynTrackObj` and `TrackObj` objects; only legacy `.s` geometry keeps the rigid
object transform.

## Status

The first implementation is complete for ORTS track profiles and native TSRE
shape templates on both DynTracks and ordinary TrackObjs. It awaits route-level
visual acceptance. The specialized native TSRE crossing-tie generator remains
on its legacy transform, as do the hardcoded fallback DynTrack mesh and legacy
`.s` shapes.

## Persisted Geometry Contract

- Main and companion starts continue to use tile plus local coordinates.
- `Flex::ParallelDyntrackSections(...)` continues to derive exact parallel
  radii.
- `Flex::RigidElevationForEndpointHeight(...)` is used for rail and road
  companions, so every saved endpoint reaches the corresponding offset main
  endpoint height.
- No new world-file token or route migration is required.
- Existing `QDirection`, TSection, TDB, and RDB semantics remain unchanged.

This contract is deliberately independent from rendering. A future generator
can replace the sweep without moving already-authored road endpoints.

## Rendering Model

The implementation follows the Open Rails separation between generated path
geometry and object placement:

1. Derive the object's horizontal TDB yaw from its complete quaternion.
2. Build the ordinary complete and yaw-only object rotations, including the
   MSTS/TSRE basis flip.
3. Calculate the residual local quaternion satisfying:

   ```text
   yawOnlyFinal * bakedRotation = completeFinal
   ```

4. Apply that residual rotation to every sampled centerline position and
   tangent during ORTS-profile or native TSRE-template mesh generation.
5. Rebuild the lateral vector as `normalize(cross(worldUp, forward))` and the
   up vector as `cross(forward, lateral)`. This retains centerline elevation
   and tangent pitch while removing rigid-plane bank.
6. Draw the resulting mesh with the yaw-only object matrix.

Static TrackShape paths first compose their own local path yaw and translation
with the residual object transform. This keeps multi-section and multi-path
procedural TrackObjs in the same frame as DynTracks. Intentional
superelevation roll is applied after the upright frame is rebuilt, so removing
accidental object-plane bank does not remove profile roll.

Native TSRE baked meshes are object-owned and uncached. The prior cache was
valid only while pitch/roll lived entirely in the WorldObj matrix; sharing a
mesh after baking would reuse the wrong orientation. Existing cached native
generation remains unchanged for Rulers, legacy advanced crossing ties, and
other callers without a baked transform.

The profile's own start/end roll input remains available for static procedural
TrackObj generation. Removing the DynTrack object's accidental rigid-plane
bank does not disable intentional profile deformation.

## Removed Workarounds

- Road companions no longer copy the main road's grade.
- Procedural DynTrack generation no longer requests the 25 cm lowered road
  end apron. Instead, ORTS-profile DynTracks and procedural static TrackObjs
  use a universal 10 cm, zero-drop terminal mesh overlap. It follows the final
  tangent and grade, masks cracks between independently generated high
  profiles, and does not alter persisted track or database endpoints.
  This is a temporary compatibility mitigation, not the intended final seam
  model. Future procedural-template work should generate continuous joints
  and remove the overlap.
- The experimental Open Rails `PreserveRigidPlane` road override is reverted;
  ORTS again removes roll from generated cross-sections for roads as it does
  for other non-superelevated procedural sections.

## Verification

The `orts-profile` suite checks:

- existing straight, curve, material, LOD, handedness, and end-apron cases;
- the universal generated-track overlap extends the terminal mesh by exactly
  10 cm without lowering it;
- a pitched 90-degree curve receives the baked centerline transform;
- its left and right profile edges remain at the same height;
- yaw-only rendering composed with the baked rotation reproduces the original
  complete DynTrack world rotation;
- native TSRE right/forward bases survive matrix-to-quaternion conversion on
  a curved frame without reversing yaw or lateral handedness.

Current automated result:

```text
[tests:orts-profile] cases=25 passed=25 failed=0
```

The incremental application build succeeds. The `dyntrack-road` suite passes
7/7, and the route-backed procedural profile benchmark completed for both
native TSRE templates and ORTS profiles. The existing `flex-point` suite
still reports its pre-existing `complete TDB subsection frames` failure in
both this worktree and the unchanged main executable (49/50); this task does
not modify that code path.

## Visual Acceptance

Test on route `bbb` with both ORTS profiles and native TSRE templates:

1. straight elevated single road;
2. left and right elevated curves;
3. middle plus left/right lanes at a visible grade;
4. successive continuously placed curved pieces;
5. transitions crossing a tile boundary;
6. reload the route and confirm the same lane endpoint heights;
7. compare TSRE and the experimental Open Rails build after its road-plane
   override has been removed.
8. select an elevated static TrackObj, enable procedural replacement, and
   compare its generated path against an equivalent DynTrack.

Confirm both surface continuity and database correctness. In particular, all
lane centerlines must end at the same Y even when their stored pitches differ.

## Remaining Work

- Consolidate ORTS-profile and native TSRE-template sampling behind one
  backend-independent path-frame API; this implementation shares the transform
  contract but retains the two existing mesh generators.
- Port the specialized native advanced crossing-tie generator to the shared
  baked frame before removing its explicit legacy fallback.
- Revisit the rigid-elevation turn limit only after path-length elevation has
  a persisted cross-engine definition.
