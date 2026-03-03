# Flex JSONL capture format

Flex capture logging writes a **JSONL** file (one JSON object per line). Lines with other record types (for example candidate logs) can coexist in the same file; the `flex` test suite replays only `type = "flex_case"` lines.

## `flex_case`

Required fields:

- `type`: `"flex_case"`
- `id`: integer (monotonic per-process)
- `x1`, `z1`: integer tile coords (start)
- `p1`: `[x, y, z]` float array (start point, tile-local)
- `q1`: `[q0, yaw, q2, q3]` float array (start orientation as provided to `Flex::NewFlex`)
- `x2`, `z2`: integer tile coords (end)
- `p2`: `[x, y, z]` float array (end point, tile-local)
- `q2`: `[q0, yaw, q2, q3]` float array (end orientation as provided to `Flex::NewFlex`)
- `preferredMinCurveRadius`: float
- `success`: boolean
- `sections`: `[10]` float array (present when `success = true`)

Optional (debug) fields (may change without breaking the runner):

- `P0`, `P1`: `[2]` float arrays (2D world points used by the solver debug view)
- `yaw0`, `yaw1`: floats
- `targetPos`: `[2]` float array (target in start-local frame)
- `phi`: float (target heading in start-local frame)

## `flex_candidate` (optional)

Emitted only when `--flex-log-candidates` is enabled and the candidate is *valid*.

Fields (best-effort, may evolve):

- `type`: `"flex_candidate"`
- `caseId`: integer (links to `flex_case.id`)
- `kind`: `"straight" | "single_curve" | "clc"`
- `sectionsSolver`: `[10]` float array (solver convention)
- `sectionsDyntrack`: `[10]` float array (TSRE dyntrack convention)
- `rawLen`, `trimmedLen`, `minRadius`, `endStraightSum`: floats
- `enabledCount`: int
- `selfIntersect`, `initialWrongWay`, `meetsPreferredMin`, `bestSoFar`: booleans

