# Task 17 - TrProfile Complex Tracks

Status: steps 1-3 built on `feature/complex-tracks` (2026-11-23, the
user's go the same day), waiting for the user's check before step 4; see
"Progress". Review and the user's answers (decisions 1-31) 2026-11-23.

## Objective

In the `Forced` procedural mode, draw every track object from the track
geometry in `tsection.dat` and the route's TrProfiles, so a route needs no
GLOBAL track shapes or their textures. `tsection.dat` stays: it is the
geometry.

Plain and parallel track already work. What is missing is complex track:
shapes whose paths overlap (junctions, diamond crossings, frog pieces,
slips). For them a new family member, `ObjectType ( TRACK COMPLEX )`,
names what is rail, what is a tie, what is ballast, so the generator can lay
shared ties and, later, cut and join rails. Simple shapes keep their simple
members (`MAIN`, `LEFT`, `MIDDLE`, `RIGHT`); `COMPLEX` is chosen only where
paths interact.

A route-root file of filters assigns profiles to shapes by name (concrete,
bridge, tunnel, ...), ahead of Open Rails' guess.

Tunnels, most bridges, buffers and the other special track shapes are
profiles too (decision 7): a tunnel is track plus its tube, a bridge mostly
track without sleepers and ballast plus guard rails. Only turntables, transfer
tables and the roundhouse are left out for now.

Work order (the user): profiles by filters, then ties, then rails.

## What TSRE does today

From `TrackObj::useProceduralShape` and `OrtsTrackProfileRenderer`:

- `Superelevation` and `Forced`: a "plain" shape without a template (not a
  tunnel, not `NumPaths > 1` with `ClearanceDist` or `MainRoute`) gets the
  guessed profile (Open Rails' `GetBestTrackProfile`: `IncludedShapes` or
  texture names), else the default profile. Every path is swept on its own.
  Frog pieces have neither `ClearanceDist` nor `MainRoute`, so they count as
  plain: on PRUDNIK `DB/DB30_A1tFrog7_5dLft.s` (`TrProfile_DB30` by
  `IncludedShapes`) is drawn as two complete tracks through each other's
  ties and ballast.
- Junctions, and diamonds and slips (they have `ClearanceDist`), take the
  policy path: `Forced` resolves an empty template to `DefaultTrack`. A
  route with a `DefaultTrack` profile (TEST_PROFILES) gets every path swept
  on its own; one without (PRUDNIK) keeps the static shape. So in `Forced`
  PRUDNIK's turnouts are static points next to procedural frog pieces.
- Multi-path shapes use family roles: paths with one starting direction
  form a group, `LEFT`/`MIDDLE`/`RIGHT` (task 12). That solves parallel
  track (shared ballast between tracks), not overlapping paths.
- The only groundwork for shared ties is the dormant native generator's
  `ProceduralShape::GenAdvancedTie`: for two paths, ties every 0.65 m at
  the midpoint of both paths at equal distance, stretched by half their
  distance (vertices with x > 0 moved right, x < 0 left), turned with the
  main route or halfway between the paths. `GlobalDefinitions` is an empty
  class; the legacy `shapetemplates.dat` loading is switched off.

## Review: GLOBAL tsection.dat, A namespace

Sources:

| File | Build | Shapes | A namespace |
| --- | --- | --- | --- |
| `TS_STARTER_ROUTE/GLOBAL/tsection.dat` | 00054 | 14,632 | 1,737 |
| `OR_PSK_NEW/GLOBAL/tsection.dat` | 2.15 PL | 7,256 | 1,647 |

Tool: [`extra/tsection-review`](../../../extra/tsection-review/README.md).
`review.py` groups shapes by their path geometry (switches, rail crossings,
paths closer than a tie), not by name; `tieplan.py` is the phase 1 tie
prototype. Per-shape lists and plan images are in the assets-gen repository
(`../assets-gen` beside this checkout), `reports/complex-tracks/`:
`a-namespace-build54.csv`, `a-namespace-psk.csv`, `plans/`, `plans-steep/`
(decision 8: assets, images and reports live there, not in TSRE).

MSTS conventions the review relies on: +x right, curve angles and path
rotations positive to the right (`A1tPnt5dLft`'s diverging curve is -5),
paths listed left to right. "Xover" in MSTS names is a diamond crossing. Slips
are built from pieces: `A1tDblSlip7_5d` is a 5 m diamond, the four corners
are `A1tPnt7_5dLftSlip`/`RgtSlip` half switches.

### Groups (A namespace)

PRUDNIK counts its 49,052 TrackObjs by `SectionIdx` (geometry), whatever
their file name.

| Group | Build 54 | PSK 2.15 | PRUDNIK objects (shapes) | Examples | Needs |
| --- | ---: | ---: | ---: | --- | --- |
| 1 single path | 509 | 454 | 21,728 (173) | `A1t25mStrt`, `A1t1000r1d`, `A1tEndPnt7_5d` | `MAIN` by filter; works today |
| 2 parallel paths, separate | 453 | 404 | 4,854 (182) | `A2t100mStrt`, `A2t1000r1d` | side roles; works today |
| 3 junction | 369 | 339 | 4,749 (23) | `A1tPnt7_5dLft`, `A1tPnt7_5dLftSlip`, `A1tYPnt10d`, `A1t3WayPnt10dMnl` | `COMPLEX` |
| 4 diamond crossing | 72 | 81 | 590 (7) | `A1tDblSlip7_5d`, `A2tFrog7_5dDbl`, `A1tXover7_5d` | `COMPLEX` |
| 5 overlapping paths (frog pieces) | 75 | 88 | 2,199 (11) | `A1tFrog7_5dLft`, `A2tFrog7_5dLft` | `COMPLEX` |
| 6 tunnel | 159 | 158 | 0 | `A2t250mStrtTun`, `A1t2000r5dRndTun` | tunnel profiles (track and tube) |
| 7 bridge | 61 | 67 | 85 (22) | `A1tBrdgEnd`, `A1tBrdg100mStrt` | bridge profiles (rails, guard rails); a few 3D bridges later |
| 8 buffer | 2 | 5 | 597 (4) | `A1tbuffer-PL_1`, `A1tBuffer` | a buffer family by name filter (decision 39, step 7) |
| 8 level/pedestrian crossing | 5 | 5 | 0 | `A2t10mLvlCrs` | track profiles |
| 8 scale/water/fuel/derail/dummy | 15 | 13 | 0 | `A1t10mStrtWtr`, `A1tDerail1_5mStrt` | track profiles |
| 8 turntable/transfer/roundhouse | 9 | 14 | 96 (3) | `A1t30mRoundhouse`, `A1t24mFordito` | skipped for now (static) |
| 9 road (platforms) | 8 | 8 | 0 | `AshphaltPlat20m500r` | `ROAD` families |
| sections missing | 0 | 11 | 0 | `A1t1_405mFordito_Kieg` | none |

- Group 3 has every shape with a switch: points, wyes, three-way, curved
  points, stub points, tram points, the slips' half-switch pieces and
  complete slips (derails and scales went to group 8 by name). Group 4 has rails crossing without a switch: diamonds, double-slip
  centres, scissors middles. Group 5 has paths closer than a tie without a
  crossing: the frog pieces after the points, where XTracks has long shared
  ties.
- Rails crossing in groups 3-5 (Build 54, the shape's flattest crossing):
  170 shapes at 5° or less, 140 up to 10°, 32 up to 20°, 31 steeper
  (`A1tXover22_6d` to `A1tXover90d`, `A1txs40r*`, tram wyes, the 22.5°
  `Frog6d5mDbl` pieces); 143 have no rail crossing (short points whose frog
  is in the next piece, frog pieces, slip halves). None of the steep ones is
  used on PRUDNIK.
- Variants that change the look, not the geometry: `Concrete` (297
  shapes), `Mnl` (manual junctions, 165), `Div` (diverging main route). The
  filters are for them.

### What PRUDNIK uses

PRUDNIK's track is the A namespace under DBTracks names: 34,072 TrackObjs
have a `<skin>_A...` file name (`DB/DB30_A1t25mStrt.s`; skins `DB30`
10,940, `DB3f` 9,472, `DB3` 4,499, `DB2f` 2,471, `DB30f` 2,293, `DB2`
1,928, ...) on A-namespace `SectionIdx`. So the profile comes from the W
file's name and the geometry from `SectionIdx`, and the A namespace covers
nearly all of PRUDNIK's rail track.

Complex track: 7,538 objects of 41 shapes. Nine shapes of the 7.5° family
carry 94 %, the first target:

| Shape | Objects | Geometry |
| --- | ---: | --- |
| `A1tPnt7_5dLft` / `Rgt` | 1,293 / 1,275 | 25 m points: straight, and r 190.713 m to 7.5° ending 1.632 m out; the frog (rails crossing at 7.0°) inside |
| `A1tFrog7_5dLft` / `Rgt` | 936 / 833 | 12.87 m frog piece: the straight, and the diverging track straight at 7.5° from 1.63 to 3.35 m out |
| `A1tPnt7_5dLftSlip` / `RgtSlip` | 933 / 933 | 10 m half switch of a double slip: r 190.713 m to 3.75°, 0.41 m out |
| `A1tDblSlip7_5d` | 475 | 5 m diamond at 7.5°, the double slip's centre |
| `A2tFrog7_5dLft` / `Rgt` | 213 / 196 | the frog piece between two parallel tracks (crossover) |

The rest: the 10° r 150 m slips and points, 4.7° and 3° points, the 7.5°
scissors middle (`A2tFrog7_5dDbl`, 55), `A1tXover7_5d` (14).

## Design

### 1. Profiles by filters: the route's filter file

A file in the route root, `trackprofiles.dat` (decision 1), lists filters;
the first that matches a track object's shape name gives its profile
family. STF like the route's other `.dat` files:

```text
SIMISA@@@@@@@@@@JINX0p0t______

TrackProfileFilters (
    Filter (
        Shapes ( "*Turntable*" "*Roundh*" "*trnsfer*" )
        Profile ( DISABLED )
    )
    Filter (
        Shapes ( "A*Tun*" )
        Profile ( PL_60E1_Tunnel )
    )
    Filter (
        Shapes ( "A*Brdg*" )
        Profile ( PL_60E1_Bridge )
    )
    Filter (
        Shapes ( "*Pnt7_5d*Slip*" )
        Profile ( TrProfile_DB30 )
        ExcludedParts ( Tie Fastener Bed )
    )
    Filter (
        Shapes ( "*DblSlip7_5d*" )
        Profile ( TrProfile_DB30 )
        IncludedTrackShape ( "A1tPnt7_5dLftSlip.s"   0      0  -10      0   )
        IncludedTrackShape ( "A1tPnt7_5dRgtSlip.s"   1.632  0   -9.893 -7.5 )
        IncludedTrackShape ( "A1tPnt7_5dRgtSlip.s"  -1.632  0   14.893 172.5 )
        IncludedTrackShape ( "A1tPnt7_5dLftSlip.s"   0      0   15     180   )
    )
    Filter (
        Shapes ( "DB/DB30_*" )
        Profile ( TrProfile_DB30 )
    )
    Filter (
        Shapes ( "A*Concrete*" )
        Profile ( PL_60E1_PS94_3D )
    )
    Filter (
        Shapes ( "A*" )
        Profile ( PL_49E1_Wood_3D )
    )
)
```

- `Shapes`: patterns matched against the track object's `FileName` as the W
  file has it (folder included, `\` read as `/`), any case, `*` and `?`.
  The name only filters; the geometry always comes from `tsection.dat` by
  the object's `SectionIdx`. Being route data, the file can name a route's
  own shape sets, such as DBTracks' skins of the A namespace.
- `Kind` (optional, any of): `Single`, `Parallel`, `Complex`, `Tunnel`
  (`TunnelShape`), `Road` (`RoadShape`), from the shape's geometry
  (section 3). `Filter ( Kind ( Complex ) Profile ( DISABLED ) )` first
  keeps complex shapes static while the `COMPLEX` member is unfinished.
  The tunnel flag says nothing about a tube (about 290 flagged shapes have
  no walls, 117 tubes have no flag; `docs/msts/tsection-track-families.md`),
  so a tunnel profile is chosen by name (decision 13).
- `Profile`: a family ID (file stem). The shape takes `MAIN`,
  `LEFT/MIDDLE/RIGHT` or `COMPLEX` from it as its geometry needs, in the
  object's type (`TRACK`, or `ROAD` for road shapes). `DISABLED`: the static
  shape.
- `IncludedParts ( ... )`, `ExcludedParts ( ... )` (optional, decision
  10): which parts of the `COMPLEX` member the matched shapes draw.
  `IncludedParts` draws only those, `ExcludedParts` all but those; with
  both, the excluded ones are taken from the included ones; all when
  neither is given. Values, any case: `Rail`, `Tie` (a track's own ties and
  the shared bearers, whichever mesh), `Fastener`, `Bed` (the member's
  polylines), `ConductorRail`, `RackRail` (decision 24), `Other`
  (anything else, such as a plain `Sweep` per path), `CheckRail` (phase 2,
  decision 33), `SwitchDrive`, `SwitchIndicator` (step 7, decision 35);
  later perhaps `Blade`, `Nose`. Unknown values are warned
  about and ignored. Shapes drawn from `MAIN` or the side roles have no part
  names; there both are ignored with a warning. `ExcludedParts` suits pieces
  whose sleepers another shape lays: parts added later (blades, check
  rails) still come without an edit.
- `IncludedTrackShape ( shape x y z rotY )` (optional, repeatable,
  decision 10): another track shape's paths added to this shape's tie,
  fastening and bed planning, as if they were its own. `shape` is the
  tsection `FileName` (or its index); `x y z` metres in this shape's space;
  `rotY` degrees, positive to the right as in `SectionIdx`. The included
  shapes are laid even where a route has not placed them, as the static
  DBTracks diamond does.
- `IncludedTrackShapeParts ( ... )` (optional, one per filter, decision
  12): the parts the included shapes give, the same values as
  `IncludedParts`. Default `( Tie Fastener Bed Other )`: no rails,
  conductor or rack rails, blades, check rails or switch parts, which stay
  with the junction object that owns them (its
  switch state, later its animation). `IncludedTrackShapeParts ( Rail Tie
  Fastener Bed )` when one object should draw a whole slip (with
  `SwitchDrive SwitchIndicator`, its half switches' drives too).
- `HandOperated ( 1 )` (optional, step 7, decision 38): the matched
  shapes' switches are operated by hand; they draw the `SwitchDrive`
  templates marked `HandOperated` instead of the unmarked ones.
- `SwitchSideFlip ( 1 )` (optional, step 7, decision 36): the matched
  shapes' switch parts stand on the other side (all of them).

Order for an object (decision 2): its own `ShapeTemplate`, then the
filters, then Open Rails' guess (last: it cannot be switched off, and it
ends in the default profile). A route that wants the guess leaves those
shapes out of its filters; a last catch-all filter (`Shapes ( "*" )`) turns
the guess off. A W file's `DISABLED` always keeps the static shape. Without
the file nothing changes. Route > Reload Track Profiles reads it again.
The file is edited by hand for now; no editor for it (decision 31).

The filters are used in every mode (decision 29): wherever a track object
is drawn procedurally they give its profile, so `Superelevation`'s canted
plain track takes them too. Complex shapes are procedural only in
`Forced`; `Superelevation` keeps them static (they have no cant).

Several filters can match one shape: the first on the list takes it
(decision 25), so specific filters go above general ones (`A*Tun*` before
`A*`, DBTracks skins before a plain `A*`). A filter whose `Kind` does not
fit the object is passed over. One filter gives everything (profile,
parts, included track shapes); filters are never merged. A track object's
properties show where its profile came from: its own `ShapeTemplate`, the
filter (its line in `trackprofiles.dat` and its `Shapes` patterns), or
Open Rails' guess. No check for filters that match nothing: it would have
to read every W file of the route.

### 2. The COMPLEX member

One more member of a family file, beside `MAIN` and the side roles:

```text
TrProfile (
    ObjectType ( TRACK COMPLEX )
    Name ( "PL 60E1 concrete sleepers, complex" )
    LODMethod ( CompleteReplacement )
    ChordSpan ( 1 )
    PitchControl ( ChordLength )
    PitchControlScalar ( 10 )
    ComplexTrack (
        TieSpacing ( 0.6 )
        TiePhase ( 0.3 )
        TieGap ( 0.1 )
        MaxBearerLength ( 4.8 )
        MaxSharedAngle ( 60 )
        ConductorClearance ( 0.2 )
    )
    LOD (
        CutoffRadius ( 150 )
        LODItem (
            Name ( "rails" )
            TexName ( "pl_rail_steel.ace" )
            ShaderName ( TexDiff )
            Template3D (
                GenerationMode ( Rail )
                Shape ( "meshes/pl_60e1_rail.obj" )
                Offset ( 0 0.153 0 )
            )
        )
        LODItem (
            Name ( "sleepers" )
            TexName ( "pl_sleeper_ps94.ace" )
            ShaderName ( TexDiff )
            Template3D (
                GenerationMode ( Tie )
                Shape ( "meshes/pl_concrete_sleeper_body.obj" )
                Offset ( 0 0.145 0 )
            )
            Template3D (
                GenerationMode ( Fastener )
                Shape ( "meshes/pl_skl_fastening.obj" )
                Offset ( 0 0.165 0 )
            )
        )
        LODItem (
            Name ( "ballast" )
            TexName ( "pl_ballast_grey.ace" )
            ShaderName ( TexDiff )
            Polyline ( ... the single track's ballast cross-section ... )
        )
    )
)
```

New `Template3D` generation modes, read only in `COMPLEX` members
(decision 3):

- `Rail`: the rail section swept along every rail line: each path at
  ± half the member's `TrackGauge` (the gauge line, decision 20) plus
  `Offset`'s x outward, the distance from the gauge line to the rail
  mesh's centre (half the rail head: 0.036 m for 60E1); y and z as given.
- `Tie`: a sleeper laid by the tie planner (section 5). Its x extent is the
  tie length, its z extent the width used for collisions.
- `Bearer` (optional): the mesh for shared ties, used when the member has
  one; else the `Tie` mesh is stretched (decision 5).
- `Fastener`: placed where a rail crosses a tie or bearer, turned with the
  rail, centred on the rail's line (the `Rail` part's); `Offset`'s x is
  ignored, y and z are used. The 3D sleepers in assets-gen have their
  clips baked in; a complex member needs the sleeper body and the
  fastening as separate meshes, because on a bearer the fastenings sit
  where the rails cross it (assets-gen's families have both since step 3).
- `ConductorRail` (decision 24): a third or fourth rail, its section swept
  along every path at `Offset`'s x (the side: a set with the conductor rail
  on either side gets a family per side, chosen by name filters; 0 for a
  fourth rail), y and z as given. Cut where it would cross another track
  (section 7).
- `RackRail` (decision 24): the rack in the middle of the track, swept or
  repeated along every path at `Offset`'s x (0), cut the same way.
- `CheckRail` (phase 2, decision 33): the check rail section, swept
  opposite every crossing nose; without one the rail section. Later:
  optional `Blade` and `Nose` meshes; without them both come from the rail
  section (as now).
- Buffers need no mode (decision 39): a buffer family's `MAIN` places the
  stop mesh with `Place` at the path's end, on buffer shapes chosen by a
  name filter (decision 37; PRUDNIK: `A1tbuffer-PL_1.s` 442
  objects, `DB/DB30_A1tBuffer.s` 90, `A1tUSBuffer.s` 29, `A1tBuffer.s`
  24). No automatic placement at track database end nodes. The example:
  assets-gen's `PL_60E1_Buffer` (step 7).
- `SwitchDrive` and `SwitchIndicator` (step 7; decisions 35, 36, 38),
  optional: a family without the mesh draws none; filters turn them off
  per shape (`ExcludedParts ( SwitchDrive SwitchIndicator )`).
  - Switches: of the paths starting together (one point, one heading),
    each one parting from the set's main path (the shape's `MainRoute`,
    else the first) with a blade between them is a switch with it: plain
    and Y points one, three-way points two, a slip's half switch one (its
    paths part by 0.26 m in 10 m: where two paths part by less than a
    gauge, the blade on each side is the rail of the path the other leaves
    towards it; phase 2's blades take the same rule). The diverging side:
    where the other path leaves the main one to. The blades' heel: where a
    blade's gap from its stock rail reaches `BladeOpening` (the open
    blade's), the farther of the two: 9.4 m on 7.5° points, 15 m on 4.7°
    (PRUDNIK's DBTracks: 8.7 and 15 m).
  - Parts stand on the switch's main path, from its toe: `Offset`'s x is
    out from the track centre, its sign the default side (+ the diverging
    side, - the straight), y up, z as in every `Template3D` (-z forward,
    so -0.6 stands 0.6 m past the toe). The mesh is modelled standing
    right of the track (its origin where `Offset` puts it); on the left it
    is turned half round about its origin.
  - `SwitchDrive`: a static mesh. With `Spacing`, more along the blades
    every `Spacing` from the first, as long as at least one `Spacing` is
    left before the heel (decision 36): with `Spacing ( 6 )` one drive on
    7.5° points, two on 4.7°. The ties under a drive (those its footprint
    along the switch overlaps; none: the nearest within a tie spacing) are
    lengthened on its side until they reach under its far edge, as
    bearers (the `Bearer` mesh where the LOD has one), within
    `MaxBearerLength` and not onto another tie; the footprints are the
    drawn drive templates' of the nearest LOD having any.
  - Hand operation (decision 38): a template marked `HandOperated ( 1 )`
    is the part the player operates by hand (a hand lever; later perhaps a
    motor's crank). A filter's `HandOperated ( 1 )` makes the switches it
    matches hand-operated: they draw the marked drive templates instead of
    the unmarked ones (the motor); every other switch draws the unmarked
    ones. `SwitchSideFlip ( 1 )` puts all of a switch's parts on the
    other side.

    ```text
    Template3D (
        GenerationMode ( SwitchDrive )
        Shape ( "meshes/pl_point_motor.obj" )
        Offset ( 1.55 0.145 -0.6 )
        Spacing ( 6 )
    )
    Template3D (
        GenerationMode ( SwitchDrive )
        Shape ( "meshes/pl_point_lever.obj" )
        Offset ( 1.5 0.145 -0.6 )
        HandOperated ( 1 )
    )
    Template3D (
        GenerationMode ( SwitchIndicator )
        Shape ( "meshes/pl_switch_indicator_main.obj" )
        Shape ( "meshes/pl_switch_indicator_other.obj" )
        Offset ( 1.9 0.145 -1.2 )
    )
    ```

    Using it belongs to the junction-state task: the walking character's
    usables (characters task 03: within `Character::UseReach`, a label
    under the view's mark, E or a click) get their first kind standing in
    the world, not in a car; using it throws the switch through the
    simulation, and blades, lever and indicator move. Static MSTS shapes
    alike by the lever's matrix name (DB: `S1_HEBEL`), as cab controls.
    Task 17 gives the format and where the marked part stands.
  - `SwitchIndicator`: one per switch, placed as a drive without
    `Spacing`. Its `Shape` list follows the switch's routes, main route
    first; one shape shows no state. Until the junction-state task it
    shows the main route (the first shape), as the blades do.
  - Defaults from PRUDNIK's DBTracks (`GLOBAL/SHAPES/DB`, measured
    2026-11-24): 7.5° and 10° points, blades 8.7 m from the toe: one
    motor, 0.95 to 2.34 m from the track centre, from the toe to 0.5 m
    past it; the indicator 1.8 m out, 0.7 to 1.0 m past the toe, its head
    0.6 to 1.0 m up. `DB2_a1tPnt4_7d*`, blades 15 m: two motors, at the toe
    and 5 m on, each with rods to both blades. The side varies within the
    set: `DB30_A1tPnt7_5dLft` has its motor on the straight side,
    `..._Lft_M2` and `..._Rgt_M2` on the diverging side.
  - In MSTS shapes the motors are static geometry; blades, the hand lever
    and the indicator head are animated matrices with the junction's keys
    (DB's indicator head turns 90° from key 0 to key 1). The junction-state
    task poses them with the blades.

Stretching (bearers, ballast): vertices with x < 0 follow the left end, x > 0
the right end, as `GenAdvancedTie` did. `StretchZone ( x0 x1 )` on a
`Tie`/`Bearer` moves the stretched band (vertices between x0 and x1 are
spread, the ones outside keep their distance from their end); default
`( 0 0 )`. A mesh with a vertex column at x = 0 stretches cleanly.

Polylines in a `COMPLEX` member at or below the rail foot (the `Rail`
part's `Offset` y) are the bed: the single track's cross-section laid under
every track, each side reaching to the midpoint between it and its
neighbour (section 6, decision 40). Higher ones (a tunnel's tube) are
stretched across the group (left half under the leftmost track, right half
under the rightmost; a polyline wholly on one side follows that side's
track).

`ComplexTrack` values: tie spacing and phase as `Repeat`'s; `TieGap` the
least gap between two tie ends; `MaxBearerLength` the longest shared tie
before tracks get their own ties; `MaxSharedAngle` the crossing angle above
which the first track's ties run through instead (section 5; 60° by
decision 9); `ConductorClearance` the least distance kept between a
conductor or rack rail and another track's running rails (section 7).
Phase 2 adds `Flangeway` (0.045), `BladeOpening` (0.16) and
`CheckRailLength` (4.8). Tie length and width come from the `Tie` mesh.

The `COMPLEX` member is the last in a family file, with its `ObjectType`
first: Open Rails without the recommended change stops reading a file at
a `COMPLEX` member (section 10).

Plain track needs no new mode for conductor and rack rails: in `MAIN` and
the side roles they are ordinary items (a `Sweep` or a `Template3D` at
their offset), as profiles draw a contact wire today.

LODs: the plan (section 5) is made once per shape; each LOD's items are laid
on it. A LOD without a `Tie` item draws no ties (far LODs: bed and rails).

Missing `COMPLEX`: complex shapes are swept per path with `MAIN` as today.

Far LODs: the member's polylines are the bed, so rails drawn as polylines
(the 2.5D far LODs of `MAIN`) would be stretched across the group; a
`COMPLEX` member's far LOD draws its `Rail` part and the bed (assets-gen's
families: the simplified rail section, no sleeper strip).

`COMPLEX` members are read from STF profiles only: TSRE's XML profile
reader has no `Template3D`.

### 3. Shapes into groups

Geometry first, names never:

1. Sample every path (its sections from `tsection.dat`), both rails at the
   `COMPLEX` member's `TrackGauge`. `tsection.dat` gives no gauge (it writes
   1.5 for standard gauge and for 30" and 36" narrow gauge alike), so a
   narrow-gauge set gets its own family with its own `TrackGauge`
   (decision 20).
2. Paths that come closer than a tie length plus `TieGap` form a complex
   group (union of pairs). A path in no group is independent.
3. Independent paths are drawn as today (`MAIN`, or the side roles, baked
   per-object transform, superelevation).
4. Each complex group is drawn from `COMPLEX`.

A shape's kind for the filters: `Single` (one path), `Parallel` (no group),
`Complex` (at least one group), `Tunnel`, `Road` (flags). Filters are
matched before a profile is chosen, so `Kind` groups with the default tie,
2.6 m plus 0.1 m (`TrackShapeAnalysis`). A crossing of exactly
`MaxSharedAngle` stays shared (a thousandth of a degree of rounding
allowed).

Examples: `A1tPnt7_5dLft` is one group of two; `A2tPnt...` (a switch on one
of two tracks) is a group of two plus one independent track; the scissors
middle `A2tFrog7_5dDbl` is one group of four.

### 4. The reference line

Shared ties of a group lie square to one reference line:

- the group's straight path on the shape's own axis (starting at the
  origin, heading 0): the through track of points and of frog pieces. The
  next piece of the same turnout (points, then its frog piece) uses the same
  line, so bearers keep their direction across pieces;
- two straight paths that cross (diamonds): their bisector;
- no straight path (wyes, curved points): the mean of the two least curved
  paths from their shared start, by equal distance;
- otherwise the straightest, longest path parallel to the axis.

A crossing steeper than `MaxSharedAngle` has no reference: the first
path's ties run through (section 5, rule 6).

### 5. Ties (phase 1)

Prototype: `tieplan.py`; plans in assets-gen `reports/complex-tracks/plans/`
(`sheet-7_5d.png`, `sheet-10d.png`) and `plans-steep/`
(`sheet-steep-compare.png`: 45°, 60°, 90° shared on top, first track's
ties through below).

1. Stations along the reference line at `TiePhase + n·TieSpacing`. Each
   station's cut line (square to the reference) is intersected with every
   rail line of the group.
2. Whole tracks are clustered on the cut line (a track's two rails never
   part): two tracks share when their tie ends would come closer than
   `TieGap`. Then neighbouring clusters are merged while the shared tie stays
   within `MaxBearerLength` (prefer shared).
3. A cluster of two or more tracks is a bearer: from its outermost rail
   minus the tie overhang to the other side plus the overhang, square to the
   reference line.
4. A cluster of the reference track alone is its own tie on the station.
5. A cluster of another track alone (hand-over): that track's own tie,
   square to itself, where it collides with nothing laid so far; else a tie
   square to the reference line on the station. Past the reference line's
   end, the track's own ties at its own spacing, again only where they fit.
6. Crossings steeper than `MaxSharedAngle`: the first path (on the axis,
   else the straightest) gets all its ties; those whose line crosses the
   other track's rails are lengthened to carry them, while they stay within
   `MaxBearerLength`. The other paths get their own ties only where they
   collide with none.
7. Fasteners at every rail/tie crossing, turned with the rail (one where
   two rails lie on each other).
8. Past a path's end a cut line may still cross one of its rails: such a
   rail joins a cluster with whole tracks (points keep their last bearer),
   but a cluster of such rails alone lays no tie (a stub at a diamond's
   end; step 4, `tieplan.py` changed alike: 45° diamond 12 to 10 ties, 60°
   10 to 7, scissors middle 22 to 21).

On real geometry this gives the look of real turnouts: `A1tPnt7_5dLft` all
bearers (the tracks stay within 1.6 m), `A1tFrog7_5dLft` bearers for its
first half, then each track its own ties; the wye stays symmetric; the
4-track scissors middle has bearers across all four tracks.

Steep diamonds (the user, 2026-11-23, decision 9): shared bearers square
to the bisector look right up to 60° (`A1tXover45d`, `A1tXover60d`); at 90°
they run diagonally across both tracks, and the first track's ties through
are better. `MaxSharedAngle` 60°: shared up to it, through above
(`sheet-steep-compare.png` shows both ways at 45°, 60°, 90°).

The user's "start with all shared" is the rule above (decision 4: shared
bearers, then each track's own ties); other ways may become a choice in
`trackprofiles.dat` later.

### 5a. Double slips

XTracks and DBTracks build a double slip from a 5 m diamond
(`A1tDblSlip7_5d`, two paths of 5 m) and four half-switch pieces
(`A1tPnt7_5dLftSlip`/`RgtSlip`, 10 m). The static diamond carries the
sleepers and ballast of the whole slip: DBTracks' `DB2_A1tDblSlip7_5d.s` has
its mesh from z -10 to +15 m and x ±4.2 m, while
`DB2_A1tPnt7_5dLftSlip.s` is x ±1.24 m (rails and blades only). The pieces
of both tracks overlap each other at every end, so if each laid its own
ties, two objects would put ties in the same place.

The user: filters decide it (decision 10). The pieces' filter leaves out
their sleepers (`ExcludedParts ( Tie Fastener Bed )`); they keep their rails
(and later their blades and check rails, with the switch state). The
diamond's filter includes the four pieces' track shapes
(`IncludedTrackShape`, section 1), so its tie planner works on the real
paths of the whole slip: bearers over its 25 m, fastenings on every rail
that crosses them, the slip curves' rails included, and the bed under all
of it. The poses are constants of the set: on six PRUDNIK double slips
(DB2, DB3, DB30 skins) the half switches sit at the same place relative to
the diamond to 1 mm:

| Piece | x | z | rotY |
| --- | ---: | ---: | ---: |
| `A1tPnt7_5dLftSlip` | 0 | -10 | 0 |
| `A1tPnt7_5dRgtSlip` | 1.632 | -9.893 | -7.5 |
| `A1tPnt7_5dRgtSlip` | -1.632 | 14.893 | 172.5 |
| `A1tPnt7_5dLftSlip` | 0 | 15 | 180 |

Sets where the double slip is one shape with all its paths
(`A1tDblSlip10d`, 9 paths) need neither value.

Preview (`extra/tsection-review/assembly.py`, assets-gen
`reports/complex-tracks/double-slip/double-slip-7_5d.png`): PRUDNIK's
double slip UiD 157 with its four half switches; the static DBTracks shapes
textured from above, the tie plan with these filter values, and every
object laying its own ties. With the values the bearers lie square to the
slip's axis over its 25 m as on the static shape, with fastenings on the
slip curves' rails; without them the pieces' and the diamond's ties
overlap.

### 6. Ballast bed

The group's bed is swept along the reference line, on cross-sections every
0.5 m and where a track starts or ends (2 mm inside it). Each track's bed
begins and stops at its own ends, on its own end planes (square to the
track there, decision 40): the next piece's bed on that track starts on the
same plane, whatever the two pieces' reference lines (a double-track frog
piece's angled track meets the half switch or points it continues into).
Only the track's own part of its end cross-section lies on that plane; what
is cut at a midpoint between tracks stays on the cross-section, where the
neighbour's edge is. A track reaching up to 1 m past the reference line's
ends (the angled tracks of double-track frog pieces start 0.107 m before
their straights) takes the cross-sections on with it; farther, its own bed
(below). No track's bed is carried past its end any more (the 0.5 m
`reachingPlanes` of step 4, which carried a points' diverging track 0.1 m
on to the straight's end). On each cross-section, every track crossing it
lays the bed polylines as its own cross-section, each side reaching to the
midpoint between it and its neighbour there: a polyline crossing the
midpoint is cut there, one wholly past it pressed onto it at the bed's
height. With the assets-gen ballast: tracks 1.6 m apart meet at about the
crib's height, 3.3 m apart at the shoulders (0.12 m), and tracks more than
4.6 m apart leave open ground between their beds, as plain track side by
side does. Higher polylines (a tunnel's tube) are stretched from the
leftmost to the rightmost track. Between two cross-sections each track's
strip joins the one in its place across (crossing tracks swap places);
where a track starts or ends there, the same path's. Parts of a path
outside the reference line's span (before its start plane, past its end
plane) get the single track's bed, cut at that plane.

Steep crossings: two beds; the second cut where it overlaps the first
(until then they overlap as today).

### 7. Rails

Phase 1: each path's rails swept as today, except where two rail lines
lie on each other: there one of them is drawn (decision 22). Where they
coincide is found from the geometry (laterally within a few millimetres,
the same heading), so one rule covers a switch toe (the stock and blade
rails until they are a rail head apart) and dual gauge (DualTracks' common
rail along the whole piece, `docs/msts/tsection-track-families.md`): no
z-fighting, no doubled rail. Crossing rails pass through each other.

Phase 2, rails from the geometry (all of it name-free):

- Switch (two paths with one start): of each coincident rail pair, the rail
  that ends up between the two tracks' centres is the blade and closure
  rail, the other the stock rail. The blade is the rail section tapered
  over its length, lying against the stock rail for the main route (later
  from the junction's state).
- Rail crossing at angle α: of its four sectors, the acute one where both
  rails' gauge faces point away is the nose (V from the actual point, where
  it is a set width); the acute one where both face in is the throat: the
  rails coming from it end there and continue as wing rails, bent out by
  the flangeway. Diamonds also have obtuse crossings (K-crossings with point
  rails).
- Check rails opposite every nose, on the other rail of each track, inside
  the gauge by the flangeway, flared ends.

The rail lines are split at these events and every piece is swept with the
rail section (tapered or cut where needed), or with `CheckRail`, `Blade`,
`Nose` meshes when the profile has them.

Conductor and rack rails (phase 1, decision 24): each path's
`ConductorRail` and `RackRail` lines are cut where they would cross another
track: where they lie between another path's running rails or come within
`ConductorClearance` of them. So a third rail stops where a diverging
track passes over its side and starts again once that track is clear; a
fourth rail or a rack stops through the switch, where the two tracks
overlap. Where two paths' conductor rails cross each other (diamonds), the
later path's is cut (the reference path first, then by index); where they
lie on each other (a switch toe) one is drawn (decision 22). Pieces are
swept with flat ends. Fine-tuning later: the ramps at the ends, the least
length of a piece, conductor rails on long sleepers, rack points keeping
the rack on the set route.

### 8. Generation, caching, drawing

- A shape-space plan (groups, reference lines, stations, ties and bearers
  with their stretch, fasteners, bed outline, later rail pieces) made from
  the `TrackShape`, its sections, the gauge and the `ComplexTrack` values:
  new code beside `OrtsTrackProfileRenderer` (e.g. `ComplexTrackPlan`).
- The renderer lays each LOD's `COMPLEX` items on the plan and reuses what
  it has: Template3D baking, chunking, material passes, PBR, LOD ranges.
- Complex groups are drawn rigid: generated once per (track shape, profile,
  gauge, profile load) and shared by every object of that shape, drawn with
  the object's matrix like a static `.s`. No baked per-object transform, no
  superelevation: junction sections get 0 from the track database, and a
  group with cant is out of scope (decision 6). PRUDNIK's 7,538 complex
  objects use 41 shapes, so at most 41 meshes per profile, nine of them for
  94 % of the objects.
- A shape with independent paths and a group: the independent paths as
  today (per object), the group shared.
- Seams: pieces of one turnout share the reference line. Tie spacing restarts
  at `TiePhase` in every piece, as for plain track today.

### 9. Tunnels, bridges and special shapes

All by profiles now, simplified in the first steps (decision 7). The
TrProfile format already covers them.

| Group | Proposal |
| --- | --- |
| Tunnels | tunnel families chosen by name filters (decision 13; the `TunnelShape` flag does not say whether there is a tube): track plus the tube's polylines. Two-track tunnels with the side roles, each drawing its half of the tube |
| Bridges | most are track without sleepers and ballast, plus guard rails (polylines or `Sweep`): bridge families by name. First steps: bridge ends without the guard rails' curved ends. The few 3D bridges (`A1t70m7Bridge2`) later, with their own profiles |
| Buffers | a buffer family by name filter: its `MAIN` places the stop with `Place` at `PathEnd` (decision 39, step 7). PRUDNIK: 597 objects |
| Level and pedestrian crossings, water, fuel, scales, derails | track profiles |
| Embankments, cuttings, ramps (DBTracks `DB2he`, ...) | track profiles with embankment or cutting polylines, as the `bbb` route's `TrProfile_DB1_emb1`, `_cut2/4/6`, `_ramp1` (decision 17) |
| Monorails (Schwebebahn `WS`, Lartigue `LM`) | profiles of their own: the beam and supports as polylines and Template3D (decision 17) |
| Overhead wire (DBTracks `f` skins, about 15,100 PRUDNIK objects) | plain track: the profile's wire polylines, as PRUDNIK's `TrProfile_DB3f` already has; junctions: later (decision 16) |
| Track with buildings (RET stations and viaducts, CYKWORKS stations and El, PSK depot hall and gates, Morris substations) | procedural, the track only: the buildings are gone, as they would be without GLOBAL (decision 23). A route keeps a static shape with a `DISABLED` filter in its `trackprofiles.dat` or `DISABLED` on the object |
| Third and fourth rail sets (UK FineScale, Europeanbahn, `tj`, CYKWORKS, LUL), rack sets (`MT`, `Z`) | plain track: the profile's conductor or rack rail item, a family per conductor-rail side chosen by name filters; complex shapes: `ConductorRail` and `RackRail` parts cut where they would cross another track (decision 24) |
| Turntables, transfer tables, roundhouse | skipped for now: `DISABLED` by filter |
| Road shapes (platforms) | `ROAD` families, as today |

### 10. Open Rails

No Open Rails code in this task (decision 30). The user's Open Rails
working copy is on another machine (the one here is an agent's clone); a
change needs a review first, and the mainstream pull request needs tests on
Windows. The task leaves a document for the Open Rails feature PR team,
[`docs/features/openrails-trprofile-complex.md`](../../features/openrails-trprofile-complex.md)
(written 2026-11-24, after phase 2, decision 32), with what Open Rails
would need; the patch is a follow-up task.

TSRE shares the TrProfile design with Open Rails, so Open Rails will
implement complex tracks too, once the feature is finished and tested in
TSRE; another team's task (decision 34). This task's design sections are
then the shared specification, and TSRE's code the reference
implementation. What the document says now:

- Open Rails does not read the filter file yet; it keeps guessing by
  `IncludedShapes`. Nothing to do before its implementation, which reads
  the file too, so a route gets the same families in both.
- Our Open Rails branch (`77d31fbb0`, read, not run) rejects the role
  `COMPLEX` with a warning, then still reads the member: each new mode
  warns twice, and the first `LODItem` left without a valid template
  throws "missing Polylines or Template3D", which ends the whole file's
  parse. Members before `COMPLEX` load; members after it are lost; 4 to 8
  warnings per family file. Hence: `COMPLEX` is the last member of a
  family file (section 2) until Open Rails has the change.
- Recommended: `COMPLEX` a known role whose `LOD` blocks are skipped
  unread and which never enters the profile list. Open Rails then needs no
  knowledge of the new modes, now or later (`SwitchDrive`,
  `SwitchIndicator`, perhaps `Blade` and `Nose`).
  `ComplexTrack` and `StretchZone` are already skipped silently by the STF
  reader.
- The road centre roles `CENTERLEFT`, `CENTERRIGHT`, `CENTER` (section
  11) when TSRE has them; blades from the junction state follow Open
  Rails' `SwitchTrackShape`.
- Until its implementation, Open Rails keeps static shapes for junctions.
  The skip above is its first step: the implementation reads what the
  skip leaves out.

### 11. Roads

- The side roles stay (decision 14). They were made for roads first and
  work; a lane width other than the profile's needs its own profile, as a
  track spacing does.
- Gap (the user): a 2+2 road whose centre line differs from the lines
  between lanes of one direction, `|   |   ||   |   |`. Its four paths form
  one group, `LEFT MIDDLE MIDDLE RIGHT`, and one `MIDDLE` member draws the
  same edge lines on both inner lanes. New roles (decision 18) for the
  paths beside the group's centre: `CENTERLEFT` and `CENTERRIGHT` for an
  even number of paths, `CENTER` for an odd number's middle path, each
  falling back to `MIDDLE`. 2+2: `LEFT CENTERLEFT CENTERRIGHT RIGHT`; 3+3:
  `LEFT MIDDLE CENTERLEFT CENTERRIGHT MIDDLE RIGHT`; a 2-lane road stays
  `LEFT RIGHT`. Open Rails' family support (our patch) needs the same
  roles.
- Road junctions use `COMPLEX` too, as `ObjectType ( ROAD COMPLEX )`, with
  more part types (decision 15): the junction surface, kerbs and kerb
  returns, markings that stop inside the junction, stop lines, gores. Their
  design is a later step. The family review counts 296 road junction shapes
  and 59 lane changes; PRUDNIK has 149 objects on 15 junction shapes and 47
  lane changes.
- Today a road TrackObj stays static in `Forced`: an empty template
  resolves to `DefaultTrack`, which no road family has (the road default is
  `RdProfile`). Decision 19: in `Forced`, a road object that no filter
  matches falls back to `RdProfile`, as road DynTracks do; `DefaultTrack`
  stays the rail fallback.

## Steps

Work on `feature/complex-tracks` in the worktree `../TSRE5vc-complex-tracks`
(decision 26).

1. The filter file: parser, `Kind`, `IncludedParts`/`ExcludedParts`,
   `IncludedTrackShape`, the order before the guess, `DISABLED`, reload,
   used in every mode, the profile's source in the track object's
   properties; a suite.
2. Shape analysis in C++: groups, kinds, reference lines; a suite checking
   the A namespace's groups against `review.py`.
3. `COMPLEX` parsing: the role, `ComplexTrack`, the new generation modes,
   `StretchZone`; `orts-profile` cases. The first `COMPLEX` profiles in
   assets-gen (decision 28): the sleeper body and the fastening as separate
   meshes, `COMPLEX` members in the existing families' files for
   `pl-60e1-ps94-3d` and `pl-49e1-wood-3d`, and a filter file, so step 4
   has something to test.
4. Phase 1: the tie planner, bearers, fasteners, the group bed, rails with
   the switch-toe rule, conductor and rack rails cut at other tracks;
   shared cache; `TrackObj` in `Forced`. Checked on PRUDNIK's 7.5°
   turnouts with the 3D families, in a scratch route or in PRUDNIK itself
   with its `TRACKPROFILES` folder compressed first as a backup (decision
   27), and on the 10° family on a test route.
5. assets-gen (decision 8), the minimum (decision 32): one example family
   per feature, a tunnel, an open-deck bridge, a third rail; checked in
   the Shape Viewer capture on tsection shapes (no test route). Images and
   reports there.
6. Phase 2: switch blades, noses, wing rails, check rails, diamonds; the
   assets they need (the check rail section). Then the Open Rails document
   (section 10) after phase 2 (decision 32). It describes the format as of
   phase 2; the format is not finished (decision 35).
7. Switch drives and indicators (decisions 35, 36, 38: `SwitchDrive`
   with `Spacing` and `HandOperated`, `SwitchIndicator`, the filters'
   `HandOperated` and `SwitchSideFlip`, ties lengthened under drives),
   buffers by a buffer family (decision 39); one example of each in
   assets-gen; viewer captures on 7.5° points, long points, three-way
   points, a double slip, and PRUDNIK.
8. Later: slips' obtuse crossings, blades from the junction state (and
   the hand lever usable, the indicator's state), bridge guard rails'
   curved ends, 3D bridges, bed
   splits, steep crossing beds, turntables, the overhead wire on junctions,
   road junction parts (`ROAD COMPLEX`), the road centre roles, conductor
   rail ramps and rack points, DB-style 3D sleepers for PRUDNIK's
   profiles, an editor for `trackprofiles.dat`. A follow-up task: the Open
   Rails patch (section 10). Another team's task: complex tracks in Open
   Rails, after TSRE has finished and tested them (decision 34).

Each step stops for the user's visual check before the next.

## Progress

| Step | State | Commit (`feature/complex-tracks`) |
| --- | --- | --- |
| 1 filter file | checked by the user (2026-11-23) | `0e36300f` |
| 2 shape analysis | checked by the user (2026-11-23) | `0e36300f` |
| 3 `COMPLEX` parsing, first profiles | checked by the user (2026-11-23) | `0e36300f`, `eee62168`; assets-gen `b0ab7e5` |
| 4 phase 1: ties, bearers, fasteners, bed, rails | checked by the user (2026-11-23) | `86274020`, `82a68097`; assets-gen `d8294f8` |
| 5 one example per feature | built (2026-11-23) | `0c4400bc`; assets-gen `21a9e84` |
| 6 phase 2: blades, frogs, check rails | built; not checked by the user yet, not blocking (2026-11-24) | `2af05063`; assets-gen `01405ca` |
| 6 Open Rails document (section 10) | written (2026-11-24) | `docs/features/openrails-trprofile-complex.md` |
| 7 switch drives, indicators, buffers | built; waiting for the user's check (2026-11-24) | `ea30b5a9`; assets-gen `35481a3` |

Steps 1-3 (2026-11-23):

- `TrackProfileFilters` reads `<route>/trackprofiles.dat` with the route's
  profiles (also on Route > Reload Track Profiles; server clients get the
  file). `TrackObj`: own template, filters, Open Rails' guess; a filter
  naming a family the route lacks is warned about once and passed over.
  Plain track from a filter takes its family's side roles per path.
  Forced road objects without a template or filter use `RdProfile`
  (decision 19). The track object's properties show the profile's source.
- `TrackShapeAnalysis`: the port of `tieplan.py`'s groups and reference
  lines; path distances by a grid (Build 54's 14,620 shapes in 2.2 s,
  Release).
- Parsing: `ObjectType ( TRACK|ROAD COMPLEX )` (`<family>_complex`, never
  a side role or a selectable subtype), `ComplexTrack`, the part modes,
  `StretchZone`; the parts are never swept per path. The STF reader moved
  to `OrtsStf` (with line numbers), shared with the filters.
- assets-gen: `pl-60e1-ps94-3d` and `pl-49e1-wood-3d` have a `COMPLEX`
  member: sleeper body and one rail seat's fastening as separate meshes,
  a uniform concrete bearer (wood: the tie stretched), the bed; existing
  files unchanged.
- PRUDNIK in `Forced` (assets-gen `reports/complex-tracks/filters/`,
  `sheet-filters.jpg`; a link root by `checks/route-root.py`, PRUDNIK
  itself untouched): without a filter file the points stay static and the
  frog pieces take the guessed `TrProfile_DB3`; with the DBTracks skins
  filtered to the 3D families and `Kind ( Complex ) DISABLED` first, every
  junction stays static and plain track turns 3D; without that line the
  junctions are drawn per path from the families, ties through each other
  (step 4's work). Renderer captures now name a picked track object's
  profile source (`RouteEditorGLWidget::selectionObjectInfo`): the
  captures' picks read "DISABLED by a filter line 11: ..." and "filter
  line 15: ...", as the properties panel does.
- Checks: suites `track-profile-filters` (41), `track-shape-analysis` (22;
  35 with `--game-root` and `TSRE_TSECTION_REVIEW_CSV`: Build 54's 14,620
  and PSK's 7,225 shapes all of `review.py`'s kind), `orts-profile` (81),
  `procedural-policy` (15); ctest 11/11.

Step 4 (2026-11-23, the user's go the same day):

- `ComplexTrackPlan`, the port of `tieplan.py` (equal ties on 12 Build 54
  shapes): stations, clusters, bearers, hand-over, own ties, steep
  crossings; fasteners on rail centre lines (half the gauge plus the
  `Rail` part's offset); rail lines in the reference's path order, where
  one lies on an earlier drawn one (within a rail head, headings within
  3°) left out; conductor and rack lines cut (section 7); bed
  cross-sections every 0.5 m along the reference, a path ending within
  0.5 m short of the reference's end plane carried to it (points'
  diverging track ends 0.1 m short), a path's own bed outside the
  reference's span, steep crossings a bed per path. Line hits include a
  line's last point.
- `OrtsTrackProfileRenderer::buildComplexMeshes`: frames built as the
  renderer's static paths (x mirrored), so the existing vertex transforms
  (winding, normals, texture coordinates) apply; ties and bearers
  stretched by `StretchZone`; the `Rail` and conductor meshes swept as
  `Sweep`; each LOD's items. Other `Template3D` modes in a `COMPLEX`
  member are not drawn on junctions yet.
- `ComplexTrackCache`: one set per (shape, member, catalog load, filter),
  held by the objects drawing it. `TrackObj`: a complex shape whose family
  has a `COMPLEX` member draws its groups with the object's whole matrix,
  its independent paths per object as plain track; in `Forced` also frog
  pieces from the guess (decision 29), and any junction drawn from a
  template.
- The Shape Viewer capture draws profiles (ported by hand from the unmerged
  `feature/profile-capture`, `e890a4c5`, onto main's viewer: generated
  objects, framing, zoom, background; main's pitch replaces the branch's
  tilt and turns about the track's own axis, so views from above take a
  negative pitch at yaw 0) and, new, any tsection shape: `"track":
  "tsection"`, `"shape"`, `"filter"` (a filter's values). assets-gen
  `checks/complex-tracks.json`.
- Checked (assets-gen `reports/complex-tracks/step4/`): the viewer's
  junctions from above and close; PRUDNIK in `Forced` with
  `trackprofiles-prudnik-step4.dat` (the families, the slip filters of
  section 5a): bearers through the points, frog pieces shared then own
  ties, one bed across the piece joins (a 0.5 m wedge before the bed fix),
  its double slips tied by the diamond. Not yet: switch blades, noses,
  check rails (phase 2, step 6), the far LOD's sleeper look, instancing.
- Timing (Release, `TSRE_COMPLEX_PROFILE` with the suite's PSK run): the
  72 complex shapes PRUDNIK uses (7,797 objects) with `PL_49E1_Wood_3D`:
  plans 683 ms, meshes 515 ms in all, once per shape; vertex data 210 MB
  over three LODs (the wood family's fastening is 270 vertices a rail
  seat). Later: ties and fastenings instanced (the renderer's `Shared`
  geometry).

Step 5 (2026-11-23):

- assets-gen examples (`tools/assetlib/structures.py`, `track/pl-60e1-tunnel`,
  `pl-60e1-bridge-open`, `pl-60e1-third-rail`): the 3D concrete track in a
  lined tube, `MAIN` and `COMPLEX` (the tube is a polyline, so over a
  junction it stretches across the tracks like the bed: one wide tube over
  a wye); an open-deck bridge (timbers, guard rails, plate girders, no
  ballast, `MAIN` only); the 3D track with a conductor rail on the right
  (`MAIN` Sweep and insulators, `COMPLEX` `ConductorRail` part). The 3D
  track's builder shared (`track.track_3d`); the existing families
  unchanged.
- Conductor and rack rails also cut where they run closer than
  `ConductorClearance` beside an earlier one (not only across it), pieces
  under 2 m left out: on points the diverging track's third rail no longer
  shows a short piece beside the straight's.
- Checked in the Shape Viewer capture (assets-gen
  `checks/complex-examples.json`, `reports/complex-tracks/step5/`).

Step 6, phase 2 (2026-11-23, the user's go the same day):

- `ComplexTrackPlan` makes rail pieces (`RailPiece`: plain, blade, wing,
  check rail) from the rail lines:
  - Switches (two paths of a group starting together): of each pair of
    rails lying on each other at the toe, the one that ends between the
    tracks' centres is the blade, the other the stock rail, drawn whole.
    A blade lies beside its stock rail up to where they are a head apart,
    its section narrowed from 20 % at the tip; the main route's blades
    (the shape's `MainRoute`, else the first path) closed, the others
    open by `BladeOpening` (0.16 m) at the tip.
  - Crossings of two paths' drawn rails, 3° or more: a common crossing
    when the acute sector's gauge faces both look out (the nose) or both
    in (the throat). The nose's legs start where their heads touch; the
    rails from the throat stop at the knuckle, a head and `Flangeway`
    (0.045 m) from the other leg, and go on as wing rails beside it to
    1.5 m past the nose tip, flared 8 cm. A check rail opposite the nose
    on the other rail of each track, inside by a head and the flangeway,
    `CheckRailLength` (4.8 m) centred on the nose's first half, flared at
    both ends. Otherwise an obtuse crossing (a diamond's middle, a
    K-crossing): the two half-rails whose gauge faces both look out of
    their obtuse sector stay whole and meet at the crossing as a bent
    knuckle rail; the other two are point rails, starting where they are a
    flangeway from the knuckle (0.68 m from the crossing at 7.5°),
    narrowed from 20 % at the tip to the full section a head and a
    flangeway from it (first a plain gap of 1.2 m: the user's report). Both
    rails of each track have their gap at the same place, so a check rail
    opposite cannot guide: a bent check knuckle runs along both point rails
    on their gauge side, a head and a flangeway off, its legs half
    `CheckRailLength` each from where they meet, flared ends (as fixed
    K-crossings are built: a running knuckle, two point rails, a check
    knuckle).
  - Rails lying on each other elsewhere (dual gauge) still drawn once.
- `ComplexTrack` takes `Flangeway`, `BladeOpening`, `CheckRailLength`;
  the `CheckRail` part and filter part name. assets-gen: the concrete
  family's `COMPLEX` member has a UIC33-like check rail on chairs 3 cm
  above the running rails (the one example; the wood family sweeps its
  rail section).
- Shape Viewer capture: `"focus": [x, z, radius]` frames part of a shape.
  Checked (assets-gen `checks/complex-phase2.json`): 7.5° points (blades,
  the frog, check rails), the 7.5° diamond (frogs at its ends, the middle
  gaps), the scissors, 10° points.
- Three-way points (`A1t3WayPnt10dMnl`, checked 2026-11-24): four blades,
  two a side, the outer rails stock rails; the second blade of a side lies
  open beside the first rather than its stock rail (alike when open); the
  frogs where the diverging tracks cross the straight and each other.
- Not yet: ends of cut
  rails show the meshes' open insides (dark squares: end faces later);
  wing and check rails reach past a shape's end into the next piece (as
  on real track) without the next piece knowing; blades from the junction's state (a task of its own, for static
  and procedural junctions alike: MSTS junction shapes carry their blades
  on animated matrices, which Open Rails' `SwitchTrackShape` poses by
  the junction's route, key 0 on `MainRoute`, 1 off it, over half a
  second; TSRE draws them at key 0. It needs the simulation's switch
  positions sent to the views, static shapes posed by them, and the
  procedural blades as a per-object piece); `Blade`/`Nose` meshes.

Step 7 (2026-11-24, the user's go the same day):

- `ComplexTrackPlan` finds the switches (`Switch`: main and other path,
  diverging side, blade length) and places their parts
  (`switchPlacements`, `switchSide`); the ties under drives are
  lengthened (section 2). Where two paths starting together part by less
  than a gauge (a slip's half switch), the blade is found by the side the
  other path leaves towards: phase 2's blades now draw on slip halves too
  (before, both rails ran plain there).
- `SwitchDrive` and `SwitchIndicator` read in `COMPLEX` members;
  `HandOperated` on `SwitchDrive` only; a drive's `Spacing` below 0 is one
  drive. Filters: `HandOperated ( 1 )`, `SwitchSideFlip ( 1 )` (shown in
  the object's properties with the filter), the part names; included
  shapes give no switch parts by default.
- assets-gen (one example each): the concrete family's `COMPLEX` member
  has a point motor (1.1 m long, its rods in the crib between the first
  two ties; a second one 6 m on where the blades are long enough), a hand
  lever with a counterweight (`HandOperated`) and an indicator in two
  states (a white bar lying, standing), on one atlas
  (`pl_track_hardware.ace`); the tunnel and third-rail examples carry
  them too. A buffer family, `PL_60E1_Buffer`: the 3D track whose `MAIN`
  places a buffer stop of old rails, its sleepers and a short bed with
  `Place` at `PathEnd` (decision 39).
- Checked in the Shape Viewer capture (assets-gen
  `checks/complex-switch-parts.json`, OR_PSK_NEW's tsection.dat): 7.5°
  points (motor, hand lever, side flipped, parts excluded), 4.7° points
  (two motors 6 m apart), three-way points (two switches, a motor each
  side), the 7.5° double slip with its half switches' drives, the buffer
  stop; and on PRUDNIK in `Forced`
  (`reports/complex-tracks/step7/trackprofiles-prudnik-step7.dat`: plain
  DB points flipped, `_M2` not, buffers to the buffer family).
- Not yet: the parts' state (indicator, lever) and using the lever (the
  junction-state task); the motor can sink into terrain higher than the
  bed's shoulder (the route's terrain, as with DBTracks' own motors).

Bed review (2026-11-24, decision 40): the user found the bed of PRUDNIK's
scissors middle one big rectangle that does not fit the switches. Beds are
now laid per track (section 6) with cross-sections at the tracks' ends; the
gaps at the scissors' joints (terrain through up to 0.47 m of missing bed)
are gone. Tests in track-shape-analysis (the scissors' straights' bed from
0 to 12.865 m; open ground between tracks 5 m apart; tracks 2.4 m apart
meeting at the crib's height). From above the scissors middle is still one
band: its crossing tracks fill the space between the straights.

Then, on PRUDNIK's double-track frog pieces (`DB/DB3f_A2tFrog7_5dLft/Rgt`,
the user the same day): the angled track's bed was cut square to the
straights and 0.107 m short at each end, so a strip of terrain showed at
both joints. Each track's bed now ends on its own end planes and reaches
its ends (section 6); the joints meet the double slip's half switch and the
points (assets-gen `reports/complex-tracks/step7/a2tfrog-joints.jpg`). A
faint seam remains where two pieces' beds overlap, with a small flickering
patch in one spot; in the Shape Viewer alone a few centimetres stay open
between a points' straight and its diverging track's angled end, which the
next piece covers. Tests: the points' diverging bed ends at its own end,
square to it; the scissors' crossing tracks' beds from end to end, square
to them.

## Acceptance

- PRUDNIK in `Forced` with a filter file: its DB track procedural apart
  from turntables and the roundhouse; the 7.5° points, frog pieces and
  double slips without crossing ties, z-fighting ballast or doubled rails at
  the toes.
- The A namespace's complex shapes (groups 3 to 5) generate without errors
  or empty output; the counts of bearers and ties match the prototype.
- Generation time (Release) for all complex shapes PRUDNIK uses, and the
  memory of the shared meshes, recorded.
- Shapes without a `COMPLEX` member, routes without the filter file, and
  `Enabled`/`Disabled` modes unchanged.

## Decisions (user, 2026-11-23)

1. The filter file `<route>/trackprofiles.dat` as in section 1, for a
   start. Filters match the W file's `FileName`; the geometry always comes
   from `tsection.dat`. The file is route data, so it can name the route's
   own shape sets (DBTracks).
2. Order: own `ShapeTemplate`, the filters, Open Rails' guess last (it
   cannot be skipped). A route that wants the guess leaves those shapes out
   of its filters; a catch-all filter turns the guess off.
3. New `GenerationMode` values (`Rail`, `Tie`, `Bearer`, `Fastener`, later
   `GuardRail`; named `CheckRail` by decision 33).
4. Where tracks diverge: shared bearers while they are close, then each
   track its own ties square to itself (the prototype's rule, way a of
   three shown). Later perhaps a choice in `trackprofiles.dat`: ties never
   turned, or one bearer across all tracks.
5. The `Bearer` mesh when the member has one, else the tie stretched.
6. Complex groups rigid and shared, without superelevation.
7. Tunnels, bridges, buffers and the special shapes are profiles now, not
   static: tunnels are simple profiles, most bridges are track without
   sleepers and ballast plus guard rails. First steps: buffers without the
   buffer stop mesh (later the profile names it), bridge ends without the
   guard rails' curved ends; a few 3D bridges later. Turntables skipped.
   Double slips: the big shape lays the sleepers, the small junction pieces
   not, by filters.
8. TSRE docs in TSRE; assets, images, reports in the assets-gen repository
   (no heavy files in TSRE). `COMPLEX` members go in the existing families'
   files; a new test route there.
9. Steep diamonds: shared bearers up to 60°, the first track's ties
   through above (`MaxSharedAngle` 60).
10. Double slips (section 5a): `ExcludedParts ( Tie Fastener Bed )` on
    the half-switch pieces, the four pieces as `IncludedTrackShape` on the
    diamond. (First `Parts ( Rail )` and `Extend ( 10 10 )`, with the slip
    curves' fastenings missing; replaced the same day by the user's
    `IncludedTrackShape` idea, which covers them.) Double slips may be
    rethought later.
11. Filter values `IncludedParts` and `ExcludedParts` (not one `Parts`),
    `IncludedTrackShape` (not `Extend`), named as TrProfile's
    `IncludedShapes`/`ExcludedShapes`.
12. One `IncludedTrackShapeParts` per filter for all its included shapes
    (one value per `IncludedTrackShape` line would be overkill); by default
    no rail-side parts, which stay with the junction objects (their switch
    state, later animated switches). A route can include them when one
    object should draw everything.
13. Tunnels: the profile is chosen by name filters; the `TunnelShape` flag
    does not imply a tube.
14. Roads: the side roles stay (made for roads first, they work); custom
    lane widths need custom profiles. Known gap: a 2+2 road's centre marking
    (section 11).
15. Road junctions: `COMPLEX`, with more part types (section 11).
16. Overhead wire: later.
17. Embankments and monorails are drawn by TrProfiles; the `bbb` route has
    similar profiles.
18. Road centre roles `CENTERLEFT`, `CENTERRIGHT`, `CENTER`, falling back
    to `MIDDLE` (section 11).
19. `Forced`: road objects that no filter matches fall back to
    `RdProfile`.
20. The gauge comes from the profile's `TrackGauge`, as profiles have it
    already; a set of another gauge gets its own family.
21. Filters are case-insensitive. Sets whose names differ only in case
    (`fwy`/`Fwy`, `dr`/`DR_`) are told apart by longer patterns and `Kind`.
22. Rail lines that lie on each other are drawn once (switch toes, dual
    gauge's common rail): no z-fighting.
23. Track with buildings or structures is procedural too, the track only,
    without the buildings for now: without GLOBAL they would be gone
    anyway. A route designer keeps a static shape with a `DISABLED` filter
    or `DISABLED` on the world object.
24. Conductor rails (third and fourth) and rack rails are supported: on
    plain track as ordinary profile items; in complex shapes as
    `ConductorRail` and `RackRail` parts, at first only cut where they
    would cross another track or its rails (section 7). Ramps, gaps and
    rack points are fine-tuned later. (The user named third and fourth
    rails; rack rails follow the same rule until the user says otherwise.)
25. Several filters matching one shape: the first on the list takes it,
    no priorities. The track object's properties show which filter (line
    and patterns) gave its profile. No load-time check for filters that
    match nothing (too expensive: every W file).
26. The design, the family review and `extra/tsection-review` committed
    to main as the task's brief, the reports in assets-gen; the work on
    `feature/complex-tracks` in `../TSRE5vc-complex-tracks`.
27. Tests in a scratch route or in PRUDNIK itself, its `TRACKPROFILES`
    folder compressed first as a backup. 3D sleepers are needed for
    complex shapes anyway (PRUDNIK's DB profiles are polylines only).
28. Profiles first: the first `COMPLEX` members in assets-gen (sleeper
    body and fastening split) before the tie planner is tested.
29. The filters are always used, wherever a track object is drawn
    procedurally; complex shapes are procedural only in `Forced`.
30. Open Rails: no code in this task. It leaves a document with
    recommendations for the Open Rails feature PR team; the patch is a
    follow-up task (a review first; the mainstream PR needs Windows tests).
31. `trackprofiles.dat` is edited by hand for now.

32. Step 5 kept to the minimum (the user, 2026-11-23): one example family
    per feature (tunnel, bridge, third rail), no assets for phase 2 before
    it (designing them now would be redone), and the Open Rails document
    only after phase 2, which may still find the format lacking. The test
    route was dropped as well (the Shape Viewer capture draws any tsection
    shape; PRUDNIK covers placed objects); say if one is wanted.

33. The phase-2 part is `CheckRail` (opposite crossing noses, placed by the
    crossing's geometry); `GuardRail` stays the name for bridge guard
    rails, which run the whole path (ordinary profile items on plain
    track; a `COMPLEX` part, cut like conductor rails, only if a bridge
    family ever needs one). The user: one name if they were the same,
    two as they differ (2026-11-23). One example asset: the concrete
    family's check rail.
34. Open Rails implements complex tracks too (the user, 2026-11-24: the
    TrProfile design is shared with Open Rails), once the feature is
    finished and tested in TSRE. Another team's task. The design sections
    here are the shared specification and TSRE's code the reference
    implementation; porting notes are added to the Open Rails document
    when the feature is finished (decision 32's rule: no external docs
    before the format is settled).
35. The format is not finished (the user, 2026-11-24): switch motors and
    switch indicators are needed as optional `COMPLEX` parts (section 2).
    A motor is a static mesh placed with an offset; short switches have
    one, longer ones two or three. The user fine-tunes offsets in the
    template; defaults from PRUDNIK's shapes. Drawn wherever the family has
    them; filters turn them off. No further parts imagined ahead of need;
    `Buffer` is the one already needed. Design only; built on the user's
    go.
36. Decision 35's proposals taken (the user, 2026-11-24, "Recommendations
    ok"):
    - The motor's side: the sign of the family's `Offset` x gives the
      default (+ the diverging side); a filter flips it for the shapes it
      matches (DBTracks has both within one set: plain on the straight
      side, `_M2` on the diverging side).
    - Ties under the motor: the planner lengthens the ties under the motor
      mesh's footprint on its side, as bearers are stretched (DB's 7.5°
      points reach out under the motor alike).
    - Drive count: `Spacing` along the blades from the first drive, as many
      as fit with at least one `Spacing` left before the blades' heel.
37. Buffers by name filter (the user, 2026-11-24): a filter matching buffer
    shapes is enough; no automatic buffer at a path end that is a track
    database end node. That may come later in plain track generation,
    which reads the track database anyway (superelevation, task 16), as
    may the tie phase from the track database: each piece's first tie at
    the vector's distance at the piece's start modulo the spacing, so ties
    run on evenly over the joints between pieces. Not implemented (checked
    2026-11-24): `Repeat` places at `Phase + n * Spacing` from each piece's
    own start, for static track objects and DynTracks alike, so the pattern
    restarts at every joint. The data is at hand: `OrtsProfileRoll::forObject`
    already finds each object's vector sections (node, index, reversed).
38. Hand operation (the user, 2026-11-24): the part says it is operated by
    hand, so a walking player gets a label on it, as on cab controls. The
    drive part is `SwitchDrive`; a template marked `HandOperated ( 1 )` is
    the hand-operated part, and a filter's `HandOperated ( 1 )` makes its
    switches hand-operated (section 2). The label and the throwing come
    with the junction-state task.
39. Buffers (the user, 2026-11-24): nothing new in the format. A buffer
    family's `MAIN` draws the track plus the stop mesh with the existing
    `Place` mode at `PathEnd` (or `PathStart`, whichever end the shape's
    stop is at); a name filter picks it for the buffer shapes. Open Rails
    draws it today. Not a `Buffer` part (section 2's earlier plan).

40. Beds per track (the user's review, 2026-11-24, of PRUDNIK's scissors
    middle `DB/DB3f_A2tFrog7_5dDbl.s`: "one big rectangle" that does not
    fit the switches; "build and show" the same day): a group's bed is no
    longer one cross-section stretched from the leftmost to the rightmost
    track but each track's own up to the midpoint between neighbours
    (section 6), and cross-sections fall where tracks start and end (the
    0.5 m grid alone began and ended a track's bed up to 0.5 m from its
    ends: the terrain showed through at joints as dark wedges). Then (the
    user, the same day, on PRUDNIK's double-track frog pieces: "ballast of
    the middle angled track isn't angled at the ends and doesn't meet the
    switch, small gap is visible"): each track's bed ends on its own end
    planes, and a track reaching up to 1 m past the piece's span keeps its
    bed to its end (the angled track lost 0.107 m at each end). Built;
    waiting for the user's look.

## Open questions

None now.
