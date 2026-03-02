# Task 01 - Flex: Robust AutoFlex With Two Curves (DynTrack `L+C+L+C+L`)

## Objective
Replace the current Flex solver (which effectively handles only `L+C+L`) with a new solver that can connect **any two oriented endpoints** ("half-lines") using DynTrack's 5-section template:

`line + curve + line + curve + line` (unused sections disabled).

Primary motivation: cases like connecting **parallel** tangents (same direction), which require an S-shape (two curves).

## Context / Current State
Relevant code paths:
- `Flex::AutoFlex(...)` uses `TDB::findNearestNode(...)` at both ends. `src/tsre/math3d/Flex.cpp:34`, `src/tsre/tdb/TDB.cpp:593`
- `Flex::NewFlex(...)` is staged via `FlexStage` (first call stores P0, second call computes). `src/tsre/math3d/Flex.cpp:69`
- The current solver outputs essentially a single curve (`L+C+L`) and fails for parallel tangents and other common layouts.
- DynTrack parameters are a 10-float array (`dyntrackdata`) mapped to 5 sections:
  - straight sections (0,2,4): `a = length`, `r = 0`
  - curve sections (1,3): `a = signed angle (rad)`, `r = radius`
  - layout: `[a0,r0, a1,r1, a2,r2, a3,r3, a4,r4]`
  - applied by `DynTrackObj::set("dyntrackdata", ...)`. `src/tsre/world/objects/DynTrackObj.cpp:329`

Debug visualization exists already (Flex's small window) and should be kept/extended. `src/tsre/math3d/Flex.cpp:85`

Call path (editor):
- `PropertiesDyntrack::flexData(...)` -> `Flex::AutoFlex(...)` -> `DynTrackObj::set("dyntrackdata", ...)` (`sections[]` updated).
- `DynTrackObj::render(...)` / `DynTrackObj::pushRenderItems(...)` builds a `QVector<TSection>` from `sections[]`. Then:
  - if `Game::proceduralTracks` is enabled, it calls `ProceduralShape::GetShape(...)` (complex shape generator).
  - if `Game::proceduralTracks` is disabled, it calls `ProceduralMstsDyntrack::GenShape(...)` (legacy fixed-buffer generator).
  `src/tsre/world/objects/DynTrackObj.cpp:370`, `src/tsre/procedural/ProceduralMstsDyntrack.cpp:18`

## Requirements
### Functional
- Support all editor cases of "connect these two half-lines", including:
  - parallel / same direction (classic S-curve)
  - parallel / opposite direction
  - intersecting tangents
  - near-degenerate angles (almost parallel) and very short distances
- Use up to 5 DynTrack sections in preferred order `L+C+L+C+L`.
- Disable unnecessary sections cleanly by outputting zeros (and always clear all 10 floats).
- Prefer a **practical, non-intersecting** two-curve solution when a one-curve solution exists but is unusable (self-intersection / crossing).
- Endpoints are **directed**: do not flip endpoint yaw by 180 degrees to make a solution fit.
- Keep the resulting centerline path reasonably bounded:
  - if total path length exceeds ~`2048m` (world tile size), **trim** the generated dyntrack to the first `2048m` instead of failing. This enables iterative long-distance routing by attaching a new dyntrack at the trimmed end. Future work may split a long solution into multiple shorter dyntracks automatically.

### API / Structure
- Introduce a new two-endpoint solver (recommended: keep the name `NewFlex` for the new API):

  `Flex::NewFlex(x1, z1, p1, q1, x2, z2, p2, q2, dyntrackSections)`

  where `q1/q2` are the same arrays filled by `TDB::findNearestNode(...)` (today `q[1]` is the yaw used by Flex).

- Keep the existing staged function for regression/debugging:
  - rename to `Flex::NewFlexDeprecatedStaged(...)` (or similar)
- Update `Flex::AutoFlex(...)` to call the new 2-point solver directly.

## Design: Geometry Model
Work in **2D (XZ plane)**. Elevation stays in `AutoFlex` (it already computes grade from endpoint delta). `src/tsre/math3d/Flex.cpp:63`

### Coordinate Normalization
Make the solver translation/rotation invariant:
1. Convert both endpoints into a single 2D space (same tile origin).
   - Existing code already applies `2048*(tileDelta)` and flips Z. Reuse the same convention to avoid sign surprises. `src/tsre/math3d/Flex.cpp:117`
2. Transform into a **start-local frame** where:
   - start point is `(0,0)`
   - start tangent is "forward" (local +Y)

This reduces to:
- Start pose: `P0=(0,0)`, heading `0`
- End pose: `P1=(dx,dy)`, heading `phi` (relative)

### Output Template
Return 5 DynTrack sections:
- `L0` (start straight) length
- `C1` (curve 1) angle/radius
- `L1` (middle straight) length
- `C2` (curve 2) angle/radius
- `L2` (end straight) length

Encode into the 10-float output array:
`[L0,0, A1,R1, L1,0, A2,R2, L2,0]`

## Proposed Solver: Two Curves + Tangent, With Search
Goal: always find a feasible `L+C+L+C+L` solution with minimal fragile math.

### Preference: Radius First (Configurable)
AutoFlex should prefer **larger radii** (gentler curves). Add an option:
- `preferredMinCurveRadius` (meters, 0 disables the preference; default **200m** for now)

Behavior:
- If a simple solution (e.g. `L+C+L`) exists that meets `min(R) >= preferredMinCurveRadius` and is non-self-intersecting, keep it.
- Otherwise, allow two-curve solutions and score by a radius-first cost function (still respecting feasibility and non-intersection).

Implementation note:
- make `preferredMinCurveRadius` a parameter to `Flex::AutoFlex(...)` (future UI can surface it).

### Key Principle: Validate With DynTrack Kinematics
Avoid sign/handedness mistakes by evaluating candidates using the same motion model DynTrack uses (mirroring `ProceduralMstsDyntrack`'s `offpos/offrot` progression). `src/tsre/procedural/ProceduralMstsDyntrack.cpp:38`

Conceptually define helpers:
- `ApplyStraight(pos, heading, length)` (heading unchanged)
- `ApplyCurve(pos, heading, angle, radius)` (heading += angle)
- `SimulateSections(...) -> (endPos, endHeading)`

Every candidate path is accepted only if simulation error is below tolerance.

### Preferred Shape: `C + L + C` (with `L0=L2=0`)
First try the simplest practical form:
- `L0 = 0`
- `C1(angle=alpha, radius=R1)`
- `L1(length=d)`
- `C2(angle=beta, radius=R2)`
- `L2 = 0`

Constraint:
- `alpha + beta = phi`

This already covers:
- S-curves (alpha and beta opposite signs)
- compound curves (alpha and beta same sign)
- "almost straight" (small angles)

### Solving Strategy (per radius pair)
For a given `(R1, R2)` and a chosen `phi`:
1. Choose `alpha` in a bounded range (see limits below).
2. Compute `beta = phi - alpha`.
3. Compute the position after `C1` and the local direction of the middle tangent.
4. Solve for `d` such that the end of `C2` lands on the target.

To avoid a fragile 2D Newton solver, reduce the solve to 1D:
- For each `alpha`, compute where the start of curve2 must be so that curve2 ends at the target.
- The vector between "after curve1" and "before curve2" must be colinear with the middle tangent direction.
- Root-find on the scalar cross product (colinearity condition), then compute `d` by projection.

Keep all roots, then filter by constraints and score.

### Search Space
Because this is an editor tool, a small discrete search is acceptable:
- Radii:
  - start with `R1 = R2` from a preferred radius set (example: `{50, 75, 100, 150, 200, 300, 500, 800, 1200, 2000, 5000}`)
  - if no solution, expand to a small grid over `(R1, R2)` from the same set
- `alpha` scan:
  - scan `alpha` in small steps (e.g. 0.5-1.0 deg) to locate sign changes for the root function, then refine by bisection

### Note: "Magic" Analytic Solutions (Dubins Paths)
There is a well-known closed-form family for connecting two **directed** poses in a plane with a **minimum turning radius**: Dubins paths (candidate types like `LSL`, `RSR`, `LSR`, `RSL`, plus `LRL`/`RLR` for the `CCC` case).

Why we still keep a small search in this task:
- our objective is not "shortest path for a fixed min radius"; we prefer larger radii, avoid self-intersection, and enforce engine limits (buffer budget / `2048m` trimming).
- we also allow the two curves to use different radii (`R1 != R2`), which moves the solver away from a single neat closed form.

However, Dubins `CSC` formulas are still useful as a fast candidate generator when we test `R1=R2=R` for a small set of `R` values starting at `preferredMinCurveRadius`.

### Limits / Constraints (practical)
Reject or heavily penalize candidates that violate:
- `radius < R_min` (UI min is 15; rails likely want higher defaults)
- `|angle| > angle_max` (UI uses approx `pi`; staying within `[-pi, +pi]` avoids loops)
- any straight length `< 0`
- self-intersection (see below)

### 2048m Trimming (Solver Output)
If the chosen solution's total path length exceeds `2048m`, trim it to a prefix of length `2048m` instead of failing.

Implementation sketch for `L+C+L+C+L`:
- Let section lengths be:
  - straight: `len = L`
  - curve: `len = abs(angle) * radius`
- Walk sections in order, subtracting from a remaining budget `B=2048`:
  - if a section fits (`len <= B`): keep it, `B -= len`
  - if not:
    - straight: set `L = B` and zero all remaining sections
    - curve: set `angle = sign(angle) * (B / radius)` and zero all remaining sections
    - stop (`B=0`)

Notes:
- This produces a valid dyntrack that does not reach the target (by design) but preserves initial heading/curvature so the user can continue from the trimmed end.
- If trimming is enabled, self-intersection checks and scoring should be applied to the *trimmed* path (geometry beyond `2048m` will never be generated).

### Candidate Scoring
When multiple feasible solutions exist, choose the "best" by cost:
- prefer larger radii (gentler): maximize `min(R1,R2)` and strongly penalize radii below `preferredMinCurveRadius`
- second-order: prefer shorter overall length: `|alpha|*R1 + d + |beta|*R2`
- prefer fewer enabled sections:
  - if `d` is near zero, allow disabling the middle straight
  - if `alpha` or `beta` is near zero, allow disabling one curve
- add a strong penalty for any self-intersection

Note: `L1=0` (curve-curve with no straight between) is allowed. In the future, we may add an option like `preferredMinStraightBetweenCurves` to bias away from reverse curves.

### Self-Intersection Avoidance
Approximate the candidate path into a polyline and reject if it intersects itself:
- sample each curve into N points (e.g. N=20-50 based on |angle|)
- combine with straight segments
- check segment-segment intersection (can reuse `Intersections::segmentIntersection`). `src/tsre/math3d/Intersections.h:18`

This enforces the "practical roads/tracks" constraint without complex analytic edge cases.

## Engine Constraints: Procedural DynTrack Buffer Limits
When `Game::proceduralTracks` is disabled, `ProceduralMstsDyntrack::GenShape(...)` writes into two fixed-size float buffers:
- `pd = new float[55000]` and `sk = new float[55000]` with **no bounds checks**. `src/tsre/procedural/ProceduralMstsDyntrack.cpp:19`

Empirical code-structure counts (each vertex is 9 floats, VNTA):
- Each enabled **straight** section contributes:
  - `pd`: 54 floats (6 vertices)
  - `sk`: 270 floats (30 vertices)
- Each **curve subdivision step** (the `0.03 rad` step loop) contributes:
  - `pd`: 54 floats (6 vertices)
  - `sk`: 324 floats (36 vertices)

The number of curve steps is approximately `ceil(|angle| / step)`, where `step` is currently hard-coded as `0.03 rad`. `src/tsre/procedural/ProceduralMstsDyntrack.cpp:446`

This means **two large curves can overflow** the `sk` buffer with the default `0.03` step. Until `GenShape` is hardened, AutoFlex must enforce a hard safety constraint such as:
- `sum( ceil(|angle_i| / 0.03) ) <= 160` across all enabled curve sections (or an equivalent angle-sum limit with margin).

If a candidate violates the buffer constraint, reject it (or fall back to a different candidate family).

More explicit (safe-side) check for a candidate with `S` enabled straight sections and curve steps `N = sum(ceil(|angle|/0.03))`:
- required: `270*S + 324*N <= 55000` (this is the `sk` buffer; `pd` is not the limiting factor).

### Hardening Plan (recommended)
Even with AutoFlex constraints, bad `dyntrackdata` can be produced by other code paths or file edits. To prevent memory corruption:
- Add runtime pre-checks in `ProceduralMstsDyntrack::GenShape(...)` and adjust quality to fit:
  - compute enabled straight count `S` and curve angles; compute `Nmax = floor((55000 - 270*S)/324)` (sk budget for curve steps).
  - if the default `step=0.03` would exceed `Nmax`, **downsample** curves by increasing `step` until `sum(ceil(|angle|/step)) <= Nmax`.
  - if even an increased `step` would still be too large (or `Nmax` is too small), **trim the rendered mesh** (stop generation when buffers would overflow).
  - compute total centerline length `L = sum(section.getDlugosc())`; if `L > 2048`, **trim the rendered mesh** to the first `2048m`.
- Switch `pd/sk` from fixed raw arrays to resizable containers (`std::vector<float>` / `QVector<float>`) sized from the computed budget, so valid-but-borderline tracks cannot overflow.

Until this hardening is implemented, **AutoFlex must enforce the current 55k-float limit** to avoid crashing the procedural generator.

This is in-scope as a safety fix for AutoFlex work (procedural dyntrack is the direct consumer).

Performance note:
- `ProceduralMstsDyntrack::GenShape(...)` can run during normal gameplay (not only in-editor). Keep hardening lightweight:
  - avoid heavy logging
  - avoid unbounded loops: compute `step` with a small bounded loop or a tiny lookup table (e.g. try a few step values until it fits)
  - pre-allocate once (no repeated push-backs causing reallocations in hot loops)

## Note: ProceduralShape Generator (Out of Scope)
The advanced generator `ProceduralShape::GenShape(...)` builds meshes from OBJ/template definitions and uses large fixed buffers internally (e.g. `new float[2000000]` / `new float[4000000]`). It is harder to predict required output size up-front, so it needs its own safety strategy (separate task).

## Fallbacks (for truly hard layouts)
If no feasible `C+L+C` exists within constraints:
1. Allow end tangents (`L0` and/or `L2`) to become non-zero:
   - search small discrete values first (0, 1, 2, 5, 10, 20, ...)
   - cost-penalize them so the solver uses them only when needed
2. As a last resort, allow solutions with very large radii (approaching straight) or split large heading changes across both curves.

## Debug / Test Window Improvements
Extend the existing Flex debug window to draw:
- start/end rays
- the sampled polyline of the final candidate path
- key points: curve start/end, tangent points, circle centers (optional)

This makes it possible to validate tricky cases quickly while tuning heuristics.

## Migration Plan (No Behavior Surprises)
1. Add new `Flex::NewFlex(...)` (two endpoints at once).
2. Rename the current staged `Flex::NewFlex(...)` to `Flex::NewFlexDeprecatedStaged(...)` and keep it callable for regression/testing.
3. Update `Flex::AutoFlex(...)` to call the new solver.
4. Audit call sites:
   - `Route::makeFlexTrack(...)` is currently not used by the editor flow; keep it calling the deprecated staged API for now (or optionally update it later to collect two points before calling the new solver). `src/tsre/world/Route.cpp:1899`

## Acce ptance Criteria / Manual Test Cases
- Straight gap between colinear ends: solver produces `L` only (or near-zero curves), no artifacts.
- Intersecting tangents where a single-curve is valid: solver may choose `L+C+L`, or `L+C+L+C+L` with one curve disabled.
- Parallel tangents, same direction, lateral offset: solver produces a clean S-curve (two curves; optional middle tangent).
- Parallel tangents, opposite direction: solver produces a feasible connection without loops.
- Far target (requires > `2048m`): solver trims the output to exactly `2048m` and produces a stable continuation direction at the end.
- Very short distance + large heading change: solver still returns a feasible path (may require end tangents and/or tighter radii).
- No self-intersecting geometry for any of the above.

## Decisions Captured
- Endpoints are directed (no yaw+pi).
- UI limits are for manual typing only; AutoFlex can exceed them, but must respect engine safety constraints.
- `preferredMinCurveRadius` is an `AutoFlex(...)` parameter; default **200m**.
- First version: if total path length exceeds ~`2048m`, trim output to the first `2048m`.
- Add runtime safety checks (or dynamic buffers) in `ProceduralMstsDyntrack::GenShape(...)` to prevent buffer overflow/memory corruption.
- For `GenShape` hardening, prefer downsampling curves first; trimming is a last resort, and changes should be lightweight to avoid runtime stutters.

## Remaining Open Questions
- None for this task (design locked); implementation details can be refined while coding.
