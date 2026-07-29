# Task 04 - Automated TDB Comparison Test For Group Synthetic `TrackShape`

Category: Group objects

## Objective
Add an automated test that compares:
- TDB insertion of a grouped track layout as separate child objects
- TDB insertion of the same grouped layout as one synthetic grouped `TrackShape`

This test should tell us whether the synthetic grouped shape produces the same TDB topology as the trusted per-object path.

## Main Branch Status (July 2026)

The diagnostic suite successfully proved that normal child-by-child insertion
matches the prepared baseline and that the attempted synthetic builder does
not. The failing builder and full diagnostic suite are preserved on:

- branch `experiments/group-trackshape-v1`
- tag `group-trackshape-failed-v1`

They are not registered in the `main` test runner, so normal test runs remain
usable for unrelated development. The generic multi-suite runner and
`route-load` smoke test remain on `main`.

## Why This Is Needed
Manual visual testing is useful but not sufficient.

Current synthetic grouped `TrackShape` problems include:
- route count instability
- connector matching drift
- results depending on placement angle/location
- visual `Z` / TDB insertion tests already confirmed that the synthetic grouped shape is still wrong in practice

These are exactly the kind of failures that automated TDB comparison can catch much better than visual inspection.

## Existing Test Infrastructure To Reuse
TSRE already has:
- headless / command-line test support in `src/tsre/tests/TestRunner.cpp`
- existing Flex test infrastructure that can be expanded instead of creating a one-off test entry point

Relevant files:
- `src/main.cpp`
- `src/tsre/tests/TestRunner.cpp`
- `src/tsre/math3d/Flex.cpp`
- `docs/tasks/tracks/01-flex-autoflex-two-curves.md`

## Recommended Refactor Before Adding The New Test
The current test runner is Flex-specific.

Recommended step:
- split the current test support into more than one suite / source file
- keep Flex tests working
- add a new grouped-trackshape suite next to them

Suggested structure:
- `src/tsre/tests/TestRunner.cpp`
  - generic CLI parsing / suite dispatch
- `src/tsre/tests/FlexTestSuite.cpp`
- `src/tsre/tests/GroupTrackShapeTestSuite.cpp`

Exact file split can vary, but the goal is:
- one runner
- separate suites
- easy future expansion

## Split Plan
This task is now split into two smaller steps:

1. Expand the test runner to support multiple suites, and add a simple headless route-load smoke test.
2. Add the grouped synthetic `TrackShape` vs per-object TDB comparison test.

Step 1 proves:
- server-mode style headless route loading works in test mode
- the engine is alive and waiting for commands
- the current tile can be inspected before attempting TDB comparison logic

## Step 1 - Headless Route Load Smoke Test
Goal:
- load route `test_group_z_1` headlessly
- preload world files the same way the dedicated server does
- inspect the current tile
- print all real world objects on that tile using existing object save formatting

Recommended suite name:
- `route-load`

Expected sample result for `test_group_z_1`:
- one real object on the current tile
- serialized output should include:

```text
Static (
    UiD ( 1 )
    FileName ( tree1.s )
    Position ( 135.222 1.01438 246.62 )
    QDirection ( 0 0 0 1 )
    VDbId ( 4294967295 )
)
```

## Step 1 Status
Done.

Implemented:
- `main.cpp`
  - applies `--route` before entering test mode, so headless suites see the requested route
- `src/tsre/tests/RouteLoadTestSuite.cpp`
  - loads the route headlessly
  - mirrors server mode by setting `Game::loadAllWFiles = true`
  - reads the current tile from the preloaded route tile map
  - filters out `Tr_Watermark` helper records
  - prints real world objects using existing `WorldObj::save(...)` formatting
- `src/tsre/tests/TestRunner.cpp`
  - now exposes the `route-load` suite next to `flex`

Observed result with `test_group_z_1`:
- current tile loads successfully
- object listing prints exactly one real world object
- the printed `Static (...)` block matches the expected sample route object

Example command:

```text
TSRE5vc.exe --test --test-suite route-load --route test_group_z_1
```

## Proposed Test Scenario
Prepare a tiny route dedicated to this test:
- one tile / one `.w` file
- a few pre-placed `trackobj` pieces forming a known layout
- example: crossover made from switch + frog + flipped switch

Test flow:
1. Load the route headlessly.
2. Load the test tile and find the prepared track objects.
3. Add those objects to a temporary `GroupObj`.
4. Copy/paste the group to a chosen target TDB location.
5. Run insertion path A:
   - add each child object to TDB independently
6. Capture resulting TDB vectors / endpoints / junctions.
7. Reset to clean TDB state.
8. Run insertion path B:
   - build one synthetic grouped `TrackShape`
   - add it to TDB as one grouped insertion
9. Capture resulting TDB vectors / endpoints / junctions.
10. Compare both results.

## What To Compare
At minimum compare:
- number of vector sections
- number of endpoints
- number of junctions
- endpoint positions
- junction positions
- vector lengths / section sequences

If exact node ids are unstable, compare normalized topology instead of raw ids.

Good comparison candidates:
- ordered list of vector sections with:
  - start position
  - end position
  - section ids
- ordered list of endpoint/junction positions in route-local coordinates

## Test Output
Prefer:
- one concise pass/fail result
- optional verbose dump for mismatches

Useful mismatch output:
- expected per-object topology summary
- actual synthetic-shape topology summary
- first differing vector / endpoint / junction

## Acceptance Criteria
- tests can run headlessly from command line
- Flex tests still work
- grouped synthetic-shape test can load the prepared route/tile
- test compares trusted per-object insertion vs synthetic grouped insertion
- failures are reported with enough detail to debug route-count / connector-position errors

## Notes
- This test should be the main gate before resuming grouped synthetic `TrackShape` work for `X`.
- Until this test exists, grouped synthetic shape work is likely to keep regressing in subtle ways.
- During current `Z` debugging, synthetic grouped insertion reuses an existing child `sectionIdx` in TDB metadata.
- That is acceptable for temporary debugging, but not a correct final representation of a synthetic grouped shape.
- A final production version should likely register the synthetic grouped `TrackShape` in `TSectionDAT` (or an equivalent generated-shape registry) instead of pretending it is the anchor child's original shape.
