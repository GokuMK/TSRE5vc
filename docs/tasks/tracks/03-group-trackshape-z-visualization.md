# Task 03 - Group Synthetic `TrackShape` Debug Through `Z`

## Objective
Use the existing `Z` / `toggleToTDB` flow to inspect a grouped synthetic `TrackShape` directly in TDB, without involving grouped `X` endpoint cycling.

This task exists because grouped `X` adds too many variables at once:
- synthetic `TrackShape` generation
- `findPosition(...)`
- `defaultEnd` cycling
- final group transform application

For visual debugging we want only:
- build grouped synthetic `TrackShape`
- insert it into TDB
- inspect what vectors/endpoints/junctions were produced

## Why This Step Matters
If the synthetic grouped `TrackShape` itself is wrong, debugging grouped `X` is misleading.

`Z` is a better isolation layer because it lets us see:
- how many paths the grouped shape produced
- whether its routes/junctions are plausible
- whether TDB vectors land where the grouped layout suggests they should

without the extra placement-rotation logic that `X` uses.

## Current Implementation Status
Archived experimental implementation.

The synthetic grouped-`Z` path was useful for diagnosis, but it produced
incorrect TDB topology and is disabled on `main`. Grouped `Z` again toggles
each real child track through the original trusted path.

The complete implementation is preserved on:
- branch `experiments/group-trackshape-v1`
- tag `group-trackshape-failed-v1`

The `TrackShape*` overload of `TDB::placeTrack(...)` remains available on
`main` as reusable infrastructure.

Experimental grouped `Z` behavior preserved on that branch:
- if any child track already exists in TDB:
  - use the old per-object removal behavior
- otherwise:
  - try inserting one synthetic grouped `TrackShape` into TDB
  - if that fails, fall back to the old child-by-child insertion path

Relevant code:
- `src/routeEditor/RouteEditorGLWidget.cpp`
- `src/tsre/world/Route.cpp`
- `src/tsre/tdb/TDB.cpp`

The grouped synthetic add path:
- builds a fresh synthetic grouped shape from the current `GroupObj`
- uses the first real child `TrackObj` as anchor pose for insertion
- calls the new `TDB::placeTrack(..., TrackShape* shape, ...)` overload

Important note about `sectionIdx`:
- the current synthetic `placeTrack` path now stores `-1` as the shape id in TDB metadata
- this is intentional for debugging: it marks the grouped shape as "not a real registered `TSectionDAT` shape"
- ideally, a real final implementation should register the synthetic shape in `TSectionDAT`, similar in spirit to how dyntrack creates/generated shapes
- using `-1` is safer than pretending the grouped shape is the anchor child's original shape
- however, this is still only a debug convention; a final implementation should not rely on invalid shape ids in persistent route data

## Toggle / Fallback
The fallback is intentionally kept local and obvious in `Route::toggleToTDB(...)`.

Current switch:
- `const bool useSyntheticGroupToggle = true;`

If needed during debugging, this can be changed quickly or the fallback block can be used directly.

Old behavior kept in place:
- add every child object to TDB independently

## Limits Of This Debug Step
- This is a debug/inspection path, not a final grouped placement feature.
- Removal still uses the existing per-object logic.
- For synthetic grouped insertion, the TDB vectors currently use the anchor child `UiD`.
- For synthetic grouped insertion, the TDB metadata currently stores shape id `-1` to mark that no real registered `TSectionDAT` shape exists yet.
- This is acceptable for temporary visual debugging, but should be revisited before calling the feature complete.

## Current Visual Findings
- Visual testing already shows that synthetic grouped `TrackShape` generation still has real issues.
- The generated grouped shape is still not reliable enough to trust based on manual inspection alone.
- This confirms that automated TDB comparison tests are worth doing before resuming grouped `X` work.

## What To Observe During Manual Testing
1. Does the grouped synthetic insertion create the expected number of routes?
2. Do the inserted vectors visually match the grouped layout?
3. Do junctions appear where the layout really branches?
4. Are angled placements still worse than axis-aligned placements?
5. Does the result depend on where the group is placed in the world?

Expected bad signs:
- wrong route count
- vectors offset from the visible group
- route topology changing only because the group was moved or rotated
- axis-aligned cases looking better than angled ones

## Acceptance For This Debug Step
- grouped `Z` can attempt a single synthetic grouped insertion into TDB
- old per-child insertion path still exists as immediate fallback
- the code path is easy to switch during debugging
- the synthetic shape can now be inspected in isolation from grouped `X`
