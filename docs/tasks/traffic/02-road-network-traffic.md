# Task 02 - Road network traffic (proposal and options)

## What the user asked (2026-11-12)
- Today cars only ride along a car spawner's line. Proposal: another
  spawner kind whose car drives through the road network, chooses turns,
  and ends at, for example, a second spawner.
- The RDB is a track database, so junctions are possible in theory. Should
  traffic use the RDB's junctions, or ignore them and compute junctions from
  where road paths cross? How do games do it today?
- Road traffic should be flexible but cheap. Traffic lights come later.

Status 2026-11-12: proposal with options for the user. Nothing to build
until the user decides. Task 01's car-following lane class is the base.

## What the road data is
Measured on seven routes (table in the roadmap):
- **There are no road junctions in existing routes**, because MSTS never
  let anyone place them: it has no road junction shapes. The RDB is a TDB,
  so junction nodes are supported in principle; they need new road junction
  shape definitions (the user, 2026-11-12). No RDB on any of the routes has
  a junction node. Every lane is a vector node between two end nodes. In the
  MSTS routes no lane end touches another (PRUDNIK: 278 of 5,637 do, lanes
  continued in another node).
- **Every road is two parallel lanes**, one vector node each, 1.5 to 6 m
  apart. Nothing in the RDB says which way a lane runs. Today a car spawner
  sets the direction by the order of its two items.
- **Roads meet only geometrically.** Lanes cross other lanes at grade
  (JAPAN1 310 points, USA2 674, PRUDNIK 1,833; median angle 90°). A minor
  road's lanes end a few metres beside a major road's lane, as a T (JAPAN1
  178 lane ends, PRUDNIK 1,981).
- Roads over or under each other are told apart by height.

The RDB's junction nodes would be the right data. TSRE could place them once
it has road junction shape definitions, but existing routes have none: their
intersections are road shapes laid over each other. For those routes,
intersections must be computed from geometry, or authored again with the
new shapes.

## How games do it
- **A lane graph with intersection nodes.** City builders (for example
  Cities: Skylines), open-world games and traffic simulators (for example
  SUMO) keep roads as segments that meet at nodes. Each segment carries
  lanes. Inside an intersection, short connector lanes (turn paths) join
  each incoming lane to the outgoing lanes it may turn into. These
  connectors are generated from the segments meeting at the node, not drawn
  by hand. Tools let authors change them (lane connection editors).
- **Conflicts are worked out beforehand.** For each intersection, which
  connectors cross which (conflict points) is computed once. At run time a
  car only asks whether a conflicting connector is taken in its time window.
  Who goes first comes from priority (major road, signs), gap acceptance,
  or a traffic light's phase (a set of connectors allowed together).
- **Car following on lanes.** Each car keeps behind its leader with a simple
  model (SUMO's default is the Krauss model; IDM is common; games use
  simpler forms). It costs a few operations per car.
- **Where cars go.** City builders give every car a trip, from an origin to
  a destination by A*. Open-world games spawn ambient traffic around the
  player and let it turn at random. Out of sight it disappears, and only
  the area near the player is simulated in detail.
- **Nobody finds intersections at run time.** The graph is built once:
  authored, or generated when the map loads.

So both of the user's ideas have their place. A lane graph with junction
nodes is how traffic is represented. For MSTS content, building that graph
from where lanes cross, when the route loads, is the only way to get one.
It is also what the games' tools do inside an intersection: they generate
the connectors from the roads meeting there.

## Options
| | A. RDB junctions only | B. Graph derived from geometry | C. B, plus RDB junctions where present, plus authored hints (recommended) |
|---|---|---|---|
| Existing routes | Nothing works: no route has a junction | Works everywhere | Works everywhere |
| Authoring | New road junction shape definitions, then every intersection placed again with them | None | None to start; hints only where the guess is wrong; new routes may place real junctions |
| Open Rails | Unknown how its road traveller takes RDB junctions | Files untouched | Files untouched; hints are world-file tokens it skips |
| Cost at load | None | Tens of ms (estimate: PRUDNIK's 1,833 crossings and 5,637 lane ends on a grid) | Same as B |
| Risk | No traffic without new content | Wrong guesses at odd places (parallel roads, a road ending near another, bridges) | Guesses fixable by hints |

## Proposed design (option C)
### 1. The lane graph
- Built in memory from the loaded RDB, once per route (or on the first
  traffic), and never written anywhere (TSRE's rule: no side files).
- A **lane** is an RDB vector node, sampled into a polyline (about 2 m).
  TSRE already intersects a road line with the track database for level
  crossings (`TDB::getSegmentIntersectionPositionOnTDB` from
  `LevelCrObj::initTrItems`). The same search, road against road, finds
  lane crossings.
- **Lane pieces** are lanes cut at intersections' entry and exit points.
  They are the graph's edges, with their lengths.
- **Nodes:**
  - intersections,
  - lane ends: dead ends, and spawner places,
  - continuations: two lane ends within 1 m, joined as one lane.
- RDB junction nodes, if a route ever has them (for example made by TSRE's
  own road tools), become intersections as they are.

### 2. Intersections from geometry
- **X:** lane crossing points at grade (height difference under about 1.5
  m) grouped within about 20 m. Two two-lane roads crossing give four
  points.
- **T:** a lane end within 8 m of another lane's middle, at grade. It is
  joined with crossing points nearby into the same intersection.
- **Boundary:** each lane through the intersection is cut a few metres
  before its first conflict and after its last.
- **Connectors:** a short curve (cubic, tangent to both lanes) from each
  incoming lane piece to each outgoing one: straight on, right, left. No
  U-turns. A straight connector follows its own lane's geometry.
- **Conflict points:** where connectors cross, computed per intersection
  at build time. A four-way crossing of two-lane roads has 12 connectors
  and at most 66 pairs.
- **Priority without data:**
  - A road whose lanes run through has priority over a road that ends at it
    (the T's stem gives way).
  - Two through roads: priority to the right (to the left with left-hand
    traffic) (question 4).

### 3. Which way lanes run
- Each lane has a partner beside it. With right-hand traffic, a lane runs
  the way that puts its partner on its left. One drive side for the route
  gives every paired lane its direction.
- **The drive side** comes from the route's own spawners: each one gives
  its lane a direction, and the majority decides. JAPAN1 and JAPAN2 should
  come out left-hand and the US routes right-hand. That checks the method.
  A TRK value can override it (question 2).
- A lane without a partner (23 on PRUDNIK) runs the way a spawner on it
  says, or is not used.

### 4. Cars
- Task 01's lane class, grown:
  - each lane piece and connector holds its cars in order,
  - a car's leader is the next car on its piece, or the first car on the
    next piece of its way,
  - IDM as in task 01.
- **At an intersection's entry** a car may go on when:
  1. no car with priority will reach any of its connector's conflict
     points within its own time window there (gap acceptance),
  2. cars already inside the intersection are let through,
  3. its exit piece has room for it (it must not block the box).
  Otherwise the entry is a stop point.
- **Deadlock:** if everyone at an intersection waits longer than a few
  seconds, the car that has waited longest goes.
- **Stop points** generalise task 01's level crossings: anything on a lane
  that can be closed. Later: traffic lights and stop signs.
- **Turn indicators** (names reserved 2026-11-13, task 01's "Cars'
  lights"): a car's shape lights named `TURN_L...`/`TURN_LEFT...` and
  `TURN_R...`/`TURN_RIGHT...` (`ShapeLights::CarRole::TurnLeft`,
  `TurnRight`) are kept off on spawned cars today. Here they flash on the
  side of a turn (connector) the car is about to take, from some metres
  before the intersection until it is through: a strength switched on and
  off in `ShapeLights::switchCarLights`, about 1.5 times a second (UN ECE
  R48: 90 ± 30 flashes a minute). Shapes made since need no change.

### 5. Spawners
- **Line spawner** (MSTS and Open Rails, unchanged in meaning): cars appear
  at the begin item and leave at the end item along their lane. In the
  graph they keep straight on through intersections, obey them, and see
  every other car on their lane. That fixes two spawners on one lane.
- **Network spawner** (new): the same `CarSpawner` world object with a TSRE
  token. Open Rails skips unknown tokens there (`WorldFile.cs:1021`).
  Possible modes (question 3):
  - **to targets:** each car drives to one of the listed spawners (by UiD),
    chosen at random, by the shortest way (Dijkstra on the lane graph;
    PRUDNIK's graph is a few thousand pieces, well under a millisecond a
    search);
  - **to anywhere:** random turns until a dead end or a spawner's end,
    with a maximum length.

  A spawner's end item is also a sink: network cars that reach it leave
  there.
- **Ambient traffic** (option, later): no spawners needed. Cars appear at
  the edge of the area near the camera, on lanes chosen by a density, and
  leave when out of it, as in open-world games. Every route gets traffic;
  USA1 has 84 spawners for 686 lanes. It could be a player setting
  (Off / Spawners / Spawners and ambient).
- Editor: Car Spawner properties get the mode and its targets. The map
  draws a spawner's targets.

### 6. Where it runs, and the cost
- Only around the camera, as spawners today (tiles within the view's tile
  range). Network cars leaving that area vanish.
- On the main thread, in simulated time (task 01, question 3). Nothing in
  the simulation depends on cars. If trains must one day react to cars (a
  car stuck on a crossing), the traffic moves into the simulation thread as
  a self-contained part: it only needs the lane graph and the crossing
  states.
- **Estimate** (not measured): per car and frame, IDM, a leader and at
  most a few conflict checks: well under a microsecond. 1,000 cars: under
  1 ms a frame. Building the graph: tens of ms at load, on a worker
  (`QThreadPool`, the rule for Qt workers on MinGW).

### 7. Traffic lights (later, its own task)
- A controller per intersection: phases, each a set of connectors allowed
  together, with times.
- The lights are world objects with shapes and draw states, fed by a state
  source as signals are. They are placed in the editor and tied to the
  nearest derived intersection by position: no side files.
- A light's red is a stop point at its connectors' entry.

### 8. Hints (later, only where the guess is wrong)
- A small TSRE world object at an intersection, saved in the world file
  like any other object:
  - connectors forbidden or added,
  - priority road,
  - not an intersection (parallel roads, a bridge read wrongly).
- Placed and shown in map mode.

### 9. Seeing it
- A map layer: lanes with direction arrows, intersections, connectors, and
  the conflict points of a chosen intersection.
- `TSRE_TRAFFIC_LOG=1`: per-route counts (intersections X/T, lanes without
  a partner or direction, dead ends) and the build time.

## Steps (tentative, after the decisions)
1. Lane graph: lanes, intersections, connectors, conflicts, drive side;
   the map layer; counts on every local route; checked by eye on JAPAN1
   and PRUDNIK.
2. Line spawners' cars as graph cars: shared lanes, intersections obeyed,
   straight on.
3. Network spawners: the world-file token, properties, routing.
4. Intersection rules tuned (gap acceptance, deadlock); captures of busy
   places.
5. Ambient traffic (if wanted).
6. Traffic lights (own task).

## Questions for the user
1. **Option C?** That is: a lane graph derived from geometry at load, RDB
   junctions used where they exist, hints later.
2. **Drive side:** from the route's spawners (majority), with a TRK value to
   override it? Or only a TRK value, set in the route's properties?
3. **Network spawner:** which mode first, to targets (listed spawners) or to
   anywhere (random turns)? Is a TSRE token on `CarSpawner` the right
   place?
4. **Priority without data:** through road over ending road, and the
   right-hand rule (left-hand on left-hand routes) between equals?
5. **Ambient traffic** (no spawners needed): wanted, and when?
6. **Thread:** the main thread in simulated time now, and the simulation
   thread only if trains must one day react to cars?
7. **Line spawners' cars:** straight on along their lane (the MSTS look) but
   obeying intersections?

## Related
- `01-level-crossings.md` (the lane class, stop points, crossing states).
- `docs/tasks/tracks/05-road-flex-research-roadmap.md` (TSRE's road tools,
  which could one day write RDB junctions).
- The roadmap's table was measured with a scratch script (not in the
  repo). Step 1's counts, made with TSRE's own geometry, replace it.
