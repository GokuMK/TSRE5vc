# Open Rails and TrProfile COMPLEX Members

Recommendations for the Open Rails feature pull request that carries the
TrProfile family support
([openrails-dyntrack-shape-template-road-design.md](openrails-dyntrack-shape-template-road-design.md)),
after TSRE's
[tracks task 17](../tasks/tracks/17-trprofile-complex-tracks.md):
procedural junctions, crossings and other complex track shapes.

TSRE and Open Rails share the TrProfile design, so Open Rails will
implement complex tracks too, once the feature is finished and tested in
TSRE. That is another team's task (decision 34). Until then, the task's
design sections are the shared specification and TSRE's code the
reference implementation.

The task itself has no Open Rails code (its decision 30). Any change needs
a review first, and the mainstream pull request needs tests on Windows.
This document covers three things:

- what the profile files may now contain;
- what Open Rails does with them today, read from the code of our branch,
  not run;
- the smallest change that keeps Open Rails quiet until its own
  implementation, and leaves what it draws unchanged.

The patch is a follow-up task. Porting notes for the full implementation
come here once TSRE has finished the feature.

## Summary

- Family files can now hold a member `ObjectType ( TRACK COMPLEX )`. TSRE
  uses it to draw junctions and crossings procedurally. Until Open Rails
  implements the feature, it keeps drawing them as static shapes and does
  not need this member.
- Today Open Rails rejects the member with a warning. It then stops reading
  the file at the member's first LOD item, so any member written after
  `COMPLEX` is lost.
- Recommended for now: accept `COMPLEX` as a known role, skip the
  member's `LOD` blocks without reading them, and leave the member out of
  the profile list. That is about ten lines in `DynamicTrack.cs`. Until its
  own implementation, Open Rails then needs to know none of the new
  generation modes, including ones TSRE adds later. The implementation
  later reads what this skips.
- Until Open Rails has that change, keep `COMPLEX` as the last member of a
  family file. All of TSRE's example families do.

## What the files contain

### The COMPLEX member

A family file (`TRACKPROFILES/<family>.stf`) gets one more top-level
member beside `MAIN`, `SINGLE` and the side roles:

```text
TrProfile (
    ObjectType ( TRACK COMPLEX )
    Name ( "PL 60E1 concrete sleepers 3D, complex" )
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
        Flangeway ( 0.045 )
        BladeOpening ( 0.16 )
        CheckRailLength ( 4.8 )
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
                Offset ( 0.036 0.153 0 )
            )
            Template3D (
                GenerationMode ( CheckRail )
                Shape ( "meshes/pl_check_rail_uic33.obj" )
                Offset ( 0 0.205 0 )
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
                GenerationMode ( Bearer )
                Shape ( "meshes/pl_concrete_bearer.obj" )
                Offset ( 0 0.145 0 )
            )
            Template3D (
                GenerationMode ( Fastener )
                Shape ( "meshes/pl_60e1_concrete_fastening.obj" )
                Offset ( 0 0.145 0 )
            )
        )
    )
)
```

The real families also have a ballast `LODItem` and two farther LODs. They
are left out here, so the member can be pasted into a file as it stands.

- The member's id is `<family>_complex`. TSRE chooses it from a shape's
  geometry: paths close enough to share ties, crossings, switches. Nothing
  selects it by name.
- `ROAD COMPLEX` is reserved for road junctions; TSRE does not use it yet.
- `ComplexTrack` holds the tie planner's and the rail planner's values, in
  metres and degrees:

  | Value | Meaning | Default |
  | --- | --- | --- |
  | `TieSpacing`, `TiePhase` | as `Repeat`'s spacing and phase | 0.6, 0.3 |
  | `TieGap` | least gap between two tie ends | 0.1 |
  | `MaxBearerLength` | longest shared tie before tracks get their own | 4.8 |
  | `MaxSharedAngle` | above it, the first track's ties run through | 60 |
  | `ConductorClearance` | least distance from a conductor or rack rail to another track's rails | 0.2 |
  | `Flangeway` | flangeway width at noses, wing rails and check rails | 0.045 |
  | `BladeOpening` | gap between an open switch blade's tip and its stock rail | 0.16 |
  | `CheckRailLength` | length of a check rail opposite a crossing nose | 4.8 |

- The new `Template3D` generation modes are read only in `COMPLEX`
  members. TSRE lays these parts itself instead of sweeping them per path:

  | Mode | Part |
  | --- | --- |
  | `Rail` | running rails, switch blades, crossing noses and wing rails (the rail section) |
  | `CheckRail` | check rails |
  | `Tie` | sleepers |
  | `Bearer` | shared (long) sleepers; without it, `Tie` stretched |
  | `Fastener` | rail fastenings, where a rail crosses a tie |
  | `ConductorRail` | third or fourth rail, cut where it would cross another track |
  | `RackRail` | rack rail, cut the same way |
  | `SwitchDrive` | a switch's drive (a point motor, a hand lever), placed from each switch's toe by `Offset`; more along long blades by `Spacing` |
  | `SwitchIndicator` | a switch indicator, one per switch; one `Shape` per route, main route first |

- `StretchZone ( x0 x1 )` may appear in `Tie` and `Bearer` templates. It
  sets the band of the mesh that stretches when a tie is lengthened.
- `HandOperated ( 1 )` may appear in `SwitchDrive` templates. It marks the
  drive that switches operated by hand draw instead of the others.
- Polylines in a `COMPLEX` member are the bed: the single track's
  cross-section, laid under every track of a junction, each reaching to
  the midpoint between it and its neighbour. Polylines above the rail
  foot (a tunnel's tube) are stretched across all the tracks.
- The format is not finished. More `COMPLEX`-only modes may follow,
  perhaps `Blade` and `Nose`. The recommended change below does not need
  to know them.
- Buffer stops need nothing new: a buffer family's `MAIN` places the stop
  with `Place` at `PathEnd`, which Open Rails draws today; the filter
  file gives that family to the buffer shapes.

The members TSRE wrote before (`MAIN`, `SINGLE`, `LEFT`, `MIDDLE`,
`RIGHT`) are unchanged.

### The route's filter file

`<route>/trackprofiles.dat` holds name filters that give TSRE's static
track objects their profile family, ahead of the `IncludedShapes` guess,
and per filter the parts drawn, other track shapes laid with them,
whether their switches are operated by hand (`HandOperated`) and which
side their switch parts stand on (`SwitchSideFlip`). It is edited by
hand. Open Rails can ignore it for now. Its own
implementation should read it too, so that a route gets the same families
in both programs.

## What Open Rails does today

This section is read from our branch (`openrails-procedural-shapes` at
`77d31fbb0`), not run. It follows a family file whose members are
`MAIN, COMPLEX`:

1. `TrProfile.SetObjectType` (`Source/RunActivity/Viewer3D/DynamicTrack.cs`)
   does not know the role. It warns "Invalid TrackProfile ObjectType role
   'COMPLEX'." and sets `ObjectTypeValid = false`. The constructor goes on
   reading the member.
2. The STF reader skips `ComplexTrack ( ... )` silently, because no token
   processor takes it (`STFReader.ParseFileOrBlock`). Inside a
   `Template3D`, `StretchZone` is skipped the same way.
3. In the first `LODItem`, every template with a new mode gets two
   warnings from `TrackProfileTemplate3D`: "Skipped unknown Template3D
   GenerationMode Rail." and "Ignored invalid Template3D definition."
4. That `LODItem` is then left with no `Polyline` and no valid
   `Template3D`. `LODItem`'s constructor throws "missing Polylines or
   Template3D".
5. The exception leaves the `TrProfile` constructor and the file's parse
   loop. `LoadProfileFile` catches it and warns "Track profile STF
   constructor failed because missing Polylines or Template3D." It returns
   the members read before `COMPLEX`; the rest of the file is never read.

The results:

- Members before `COMPLEX` load and draw as before.
- Members after `COMPLEX` are lost.
- Each family file with a `COMPLEX` member logs 4 to 8 warnings on every
  load: our example families log 4 (`PL_49E1_Wood_3D`), 6
  (`PL_60E1_PS94_3D`, `PL_60E1_Tunnel`) and 8 (`PL_60E1_ThirdRail_R`).
- Junctions stay static shapes, as they always were in Open Rails.
- Selection is unaffected: `GetBestTrackProfile` considers only
  `TRACK MAIN`, and `FindFamilyRole` looks up only the named side roles.

## Recommended change for now

The change is small and alters nothing Open Rails draws. It is the first
step of Open Rails' own implementation, which later reads what it skips:

1. Add `Complex` to `TrProfile.ProfileObjectRole`, accept `COMPLEX` in
   `SetObjectType`, and return `"_complex"` from `TRPFile.RoleSuffix`. The
   suffix keeps warnings and duplicate checks readable.
2. In the STF `TrProfile` constructor, skip a `Complex` member's `LOD`
   blocks without reading them. Do not throw "missing LODs" for such a
   member.
3. In `LoadProfileFile`, do not add `Complex` members to the profile list.
   No lookup, by score, role or explicit `ShapeTemplate` name, can then
   return one. A `ShapeTemplate` naming `<family>_complex` gets the same
   warning as any unknown profile name.
4. Nothing for `ComplexTrack`: the STF reader already skips it. An
   explicit token processor that calls `stf.SkipBlock()` documents this,
   if you prefer.
5. Nothing for `trackprofiles.dat`.

Leave `ReadGenerationMode` as it is. Inside a skipped `COMPLEX` member it
never sees the new modes. In any other member, a new mode is a content
error, and the warning is right; TSRE gives a diagnostic there too.

A sketch, not compiled:

```csharp
// TrProfile
public enum ProfileObjectRole { Main, Single, Left, Middle, Right, Complex, }

// SetObjectType, role switch
case "COMPLEX": ObjectRole = ProfileObjectRole.Complex; break;

// TRPFile.RoleSuffix
case TrProfile.ProfileObjectRole.Complex: return "_complex";

// TrProfile(Viewer, STFReader)
new STFReader.TokenProcessor("lod", ()=> {
    // TSRE's procedural junction parts: Open Rails draws junctions from
    // their static shapes and never reads these LODs.
    if (ObjectTypeValid && ObjectRole == ProfileObjectRole.Complex)
        stf.SkipBlock();
    else
        LODs.Add(new LOD(viewer, stf)); }),
...
if (ObjectRole == ProfileObjectRole.Complex)
    return; // nothing to draw, no textures
if (LODs.Count == 0)
    throw new Exception("missing LODs");

// LoadProfileFile
TrProfile profile = new TrProfile(viewer, stf);
if (profile.ObjectTypeValid &&
    profile.ObjectRole != TrProfile.ProfileObjectRole.Complex)
    profiles.Add(new TRPFile(familyName, profile));
```

`ObjectType` must come before the `LOD` blocks for the skip to work. TSRE's
files put it first, as every family example does. If a hand-written
member gives it after its LODs, Open Rails reads the LODs as today, with
the same warnings and the same stop.

The XML profile reader needs no change. `COMPLEX` members exist only in
STF family files, and TSRE's XML reader has no `Template3D` at all.

## Checking it

Paste the member above into any family file that has a `TRACK MAIN`, and
put another member after it, for example a copy of `MAIN` with
`ObjectType ( TRACK SINGLE )`. Then load a route that uses the family.

| | Before the change | After it |
| --- | --- | --- |
| Warnings about the family in `OpenRailsLog.txt` | 6 | none |
| The `SINGLE` member after `COMPLEX` | lost | loaded |
| Plain track | drawn with `MAIN` | the same |
| Junctions | static shapes | the same |

TSRE's full example families (`PL_60E1_PS94_3D`, `PL_49E1_Wood_3D`,
`PL_60E1_Tunnel`, `PL_60E1_ThirdRail_R`) come from TSRE's asset generator
and are not in this repository. Ask us for a copy if a whole route is
easier to test with.

## Later

- TSRE may add road centre roles (`CENTERLEFT`, `CENTERRIGHT`, `CENTER`,
  each falling back to `MIDDLE` when a family lacks it; task 17, section
  11). Open Rails' family support would read and use them in the same way
  as the existing side roles. They are not in the format yet.
- TSRE poses junctions by a route state on the shape side, as Open Rails'
  `SwitchTrackShape` (`Viewer3D/Shapes.cs`) does: static shapes' keyed
  parts at key 0 on the main route, 1 otherwise, over half a second;
  procedural junctions' blades blended between their two routes, the
  `SwitchIndicator` and a `HandOperated` `SwitchDrive` showing the
  `Shape` for the switch's route (main route first). TSRE's simulation
  sets the state later.
- TSRE runs `Repeat` copies and texture coordinates that grow with
  distance (polylines' `DeltaTexCoord`, a `Sweep`'s path-length texture)
  on from each piece's distance along its track (road) database vector, so
  sleepers and road markings continue evenly over the joints between
  pieces; `Stretch` and `Place` are unchanged. Open Rails places them per
  piece. It could do the same from its `TrackVectorSection` data; no
  format change is involved.
- Complex tracks in Open Rails itself (another team's task), once TSRE
  has finished and tested them. The specification is the task's design,
  sections 1 to 8. The reference implementation is TSRE's planner and
  mesh builder: `src/tsre/procedural/ComplexTrackPlan.*`,
  `TrackShapeAnalysis.*`, `TrackProfileFilters.*` and
  `OrtsTrackProfileRenderer::buildComplexMeshes`. It builds the geometry in
  the shape's space, drawn rigid with each object's matrix. Porting notes,
  and the checks that show both programs lay the same ties and rails,
  come here when the feature is finished.
