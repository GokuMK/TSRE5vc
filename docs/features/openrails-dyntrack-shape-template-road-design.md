# Open Rails ShapeTemplate And Road Design/Implementation

## Purpose

This document describes and records a minimal Open Rails implementation for
DynTracks and static TrackObjs written by TSRE. It covers explicit Open Rails
track-profile selection, opt-in procedural replacement of ordinary static rail
shapes, and the rail-specific rendering behavior that must be disabled for road
DynTracks.

The original design review compared:

- local Open Rails commit
  `91414172dea8f16c08e587f2792264110aaabab1`;
- upstream `master` commit
  `b5109a8ec55ffcd5f155e49a0b3114815e166480`.

The relevant parsing, profile-loading, wire, and superelevation paths were
materially unchanged between those revisions. The implementation and visual
acceptance described below were made against the local commit. Before an
upstream submission, the five runtime-file changes should be rebased onto the
current Open Rails branch and rebuilt there.

## Required Behavior

### ShapeTemplate parsing

Add `ShapeTemplate` at the end of the Open Rails `TokenID` enum. Do not insert
it among existing tokens because their numeric values are used by the binary
MSTS reader.

Extend `Orts.Formats.Msts.DyntrackObj` with the optional template name. Parse
it with `ReadString()` and preserve it in the DynTrack subsection copy
constructor.

Also extend `Orts.Formats.Msts.TrackObj` with the optional template name.
Static TrackObj support uses the same text token and named-profile resolver.

### Road classification

For `DyntrackObj` only, interpret:

```text
StaticFlags & 0x00000100
```

as road/RDB identity. Keep this helper scoped to DynTrack rather than changing
the meaning of the general Open Rails `StaticFlag` enum.

Legacy rail DynTracks retain their established `0x00100000` value. A road
DynTrack may contain both the legacy DynTrack bit and the road bit.

### Profile identity and lookup

`TRPFile` must retain the source filename stem in addition to the declared
`TrProfile.Name`:

```text
TrackProfiles/TrProfileRoad.stf -> TrProfileRoad
```

Resolve an explicit `ShapeTemplate` case-insensitively in this order:

1. exact profile filename stem;
2. unique declared `TrProfile.Name`;
3. unresolved.

The filename stem wins because it is unique, follows Open Rails profile-file
precedence, and is the stable identifier already stored by TSRE. A declared
name is only an alias when it identifies exactly one loaded profile.

Do not use the static-track texture and shape-filter scoring code for an
explicit DynTrack name.

## Selection Table

| DynTrack state | Selected Open Rails profile |
| --- | --- |
| Rail, no `ShapeTemplate` | Existing profile index 0 |
| Valid explicit name | Named profile |
| Invalid explicit name | Profile 0 and one useful warning |
| `ShapeTemplate DEFAULT` | Profile 0 |
| `ShapeTemplate DISABLED` | Profile 0 |
| Road, no `ShapeTemplate` | `TrProfileRoad`, when present |
| Road, missing `TrProfileRoad` | Profile 0 and one useful warning |

An invalid explicit road name also falls back to profile 0. It must not
silently select `TrProfileRoad`, because an explicit but invalid choice should
have one deterministic compatibility fallback.

The initial patch should not add a canned road cross-section to Open Rails.
Falling back to profile 0 keeps the object visible and retains existing
behavior. Routes that need the road default provide
`TrackProfiles/TrProfileRoad.stf` or `.xml`.

## Rendering Integration

Resolve the profile once for the complete DynTrack and pass the selected
`TrProfile` to every generated subsection.

This is required in both branches of
`SuperElevationManager.DecomposeDynamicSuperElevation`. Before this patch, the
no-match branch omitted the profile argument and therefore silently selected
profile 0.

For road DynTracks:

- do not call `FindSuperElevationSection`;
- still construct every ordinary zero-roll procedural subsection with the
  selected road profile;
- do not call `Wire.DecomposeDynamicWire`.

Do not return early from dynamic superelevation decomposition. Despite its
name, that function is also the normal DynTrack mesh-construction path, so an
early return would make the road disappear.

Skipping the rail lookup prevents a road subsection from accidentally
receiving rail superelevation when TDB and RDB records share a world-object
`UiD`.

### Static rail TrackObj integration

Open Rails' existing static superelevation path already decomposes a safe
static TrackObj and generates procedural geometry for all of its subsections.
Previously, it retained that geometry only when at least one subsection
matched the superelevation data.

Treat Open Rails superelevation being enabled as the gate corresponding
approximately to TSRE `ProceduralTracks = Enabled`:

| Static TrackObj state | Behavior |
| --- | --- |
| Superelevation disabled globally | Render the original static shape |
| Superelevation enabled, no `ShapeTemplate` | Preserve existing Open Rails profile guessing and replacement behavior |
| Valid explicit name | Use the named profile and retain the complete procedural replacement |
| `ShapeTemplate DEFAULT` | Use profile 0 and retain the complete procedural replacement |
| `ShapeTemplate DISABLED` | Render the original static shape |
| Missing or ambiguous explicit name | Warn once and render the original static shape |

An explicitly selected profile whose `SuperElevationMethod` is `None` still
generates the procedural shape, but without visual banking.

Preserve all existing safety exclusions. Roads, tunnels, junctions,
crossovers, moving tables, and objects whose section data cannot be resolved
must continue to use their existing static rendering path. This milestone does
not add procedural static-road rendering.

## Recommended Upstream Patch Organization

Prepare one small pull request with three reviewable commits:

1. **Honor DynTrack ShapeTemplate**
   - add the text token and parsed property;
   - retain profile filename identity;
   - add deterministic named-profile resolution;
   - propagate the selected profile through every DynTrack rendering branch;
   - preserve profile-0 behavior for legacy routes.

2. **Add road DynTrack rendering behavior**
   - classify DynTrack road identity with `0x00000100`;
   - use `TrProfileRoad` as the implicit road profile;
   - suppress rail superelevation lookup and overhead wire for roads;
   - retain a visible profile-0 fallback.

3. **Honor ShapeTemplate on safe static rail TrackObjs**
   - parse the optional value on `TrackObj`;
   - use explicit names instead of profile guessing when present;
   - retain all generated zero-roll subsections for an explicit selection;
   - preserve every existing static superelevation exclusion and legacy
     behavior when the value is absent.

A general TDB/RDB resolver is not required for this first rendering patch.
Open Rails road traffic follows RDB independently. The DynTrack road
classification is needed here for profile selection and for avoiding
rail-only visual processing.

## Verification And Acceptance Matrix

| Case | Status | Evidence or remaining work |
| --- | --- | --- |
| Incremental `RunActivity` build | Passed | Build completed with zero errors |
| Legacy rail DynTrack without `ShapeTemplate` | Visually accepted | Route `bbb` retains ordinary profile-0 DynTracks |
| Rail profile selected by filename stem | Visually accepted | Multiple distinct profiles render on route `bbb` |
| Multiple named rail profiles in one route | Visually accepted | See the `bbb` profile list below |
| Straight and curved rail subsections | Visually accepted | Examined in route `bbb` |
| `ShapeTemplate DISABLED` fallback | Visually accepted | `bbb` contains a `DISABLED` DynTrack |
| `ShapeTemplate DEFAULT` fallback | Implemented, not separately examined | Resolves directly to profile 0 |
| Selection by unique declared `TrProfile.Name` | Implemented, not visually tested | Requires a profile whose declared name differs from its filename |
| Missing explicit name | Implemented, not visually tested | Falls back to profile 0 and warns once per name |
| Ambiguous declared name | Implemented, not visually tested | Falls back to profile 0 and warns once per name |
| Road DynTrack with `TrProfileRoad` | Implemented, pending assets and visual test | Requires an accepted road profile |
| Road DynTrack without `TrProfileRoad` | Implemented, not visually tested | Remains visible with profile-0 fallback and warning |
| Mixed rail and road DynTracks | Pending visual test | Requires road test objects and profile |
| Road DynTrack on an electrified route | Pending visual test | Must confirm no dynamic wire is generated |
| Road and rail records sharing a `UiD` | Pending visual test | Must confirm the road skips TDB superelevation lookup |
| Straight and curved road subsections | Pending visual test | Requires road profile and test objects |
| Road CarSpawner/RDB traversal | Out of rendering-patch scope; pending | Uses the existing independent RDB traffic path |
| Static rail TrackObj without `ShapeTemplate` | Implemented, pending regression test | Retains the existing Open Rails superelevation path |
| Static rail TrackObj with valid explicit name | Visually accepted | Named ORTS profiles render on ordinary TrackObjs in route `bbb` |
| Static rail TrackObj with `DEFAULT` | Implemented, pending visual test | Procedurally replaces the shape with profile 0 |
| Static rail TrackObj with `DISABLED` | Implemented, pending visual test | Retains the original static shape |
| Static rail TrackObj with missing name | Implemented, pending visual test | Retains the original shape and warns once |
| Excluded static shapes | Preserved by implementation, pending regression test | Roads, tunnels, junctions, crossovers, moving tables, and unresolved sections remain static |

Final road acceptance requires a visible and correct road profile, no road
wire, no road superelevation, and deterministic fallback behavior.

## Implementation Status

Implemented and rail-tested in the local Open Rails worktree on 2026-07-30:

- `ShapeTemplate` is appended to `TokenID` and parsed by `DyntrackObj`;
- subsection copies retain the template name;
- `TRPFile` retains its source filename stem and resolves explicit DynTrack
  templates by filename stem, then by a unique declared profile name;
- `DEFAULT`, `DISABLED`, missing names, and missing road defaults fall back
  deterministically to profile 0 with warnings for missing selections;
- the selected profile is passed through both elevated and ordinary DynTrack
  subsection construction;
- road DynTracks skip TDB superelevation lookup and dynamic overhead wire;
- `ShapeTemplate` is also parsed on ordinary static `TrackObj` objects;
- when superelevation is enabled, a valid explicit static-rail template
  selects its named profile and retains the complete procedural replacement,
  including shapes with no actually superelevated subsection;
- absent static templates preserve the existing Open Rails behavior, while
  `DISABLED` and unresolved names retain the original static shape;
- all existing static-track safety exclusions remain in force.

The incremental `RunActivity` build succeeds. Route `bbb` contains rail
DynTracks selecting `TrProfileExampleClean`, `TrProfileExampleClean02`,
`TrProfile_SR_w`, `TrProfile_DB1`, and `TrProfile_DB2f`, plus a `DISABLED`
case. These named rail profiles and the legacy profile-0 behavior were
visually accepted in Open Rails on route `bbb`.

Explicit named-profile replacement of ordinary static rail TrackObjs was also
visually accepted in both TSRE and Open Rails on route `bbb`.

Road profile rendering remains pending visual acceptance until a suitable
`TrProfileRoad` is available. The code currently uses the documented visible
profile-0 fallback when it is absent.

## Shareable Patch Scope

The functional patch is isolated to these five Open Rails runtime files:

- `Source/Orts.Parsers.Msts/TokenID.cs`;
- `Source/Orts.Formats.Msts/WorldFile.cs`;
- `Source/RunActivity/Viewer3D/DynamicTrack.cs`;
- `Source/RunActivity/Viewer3D/SuperElevation.cs`;
- `Source/RunActivity/Viewer3D/Scenery.cs`.

At the current local revision, this is 183 inserted lines and 39 deleted lines.
VS Code support files, project-file conversions, generated output, and other
local worktree changes are not part of the feature patch.

Useful proof for reviewers consists of:

1. this design and implementation record;
2. the clean five-file Git patch;
3. the zero-error incremental `RunActivity` build result;
4. a short before/after video or screenshots from route `bbb`;
5. the corresponding `ShapeTemplate` entries in the two tested world tiles.
