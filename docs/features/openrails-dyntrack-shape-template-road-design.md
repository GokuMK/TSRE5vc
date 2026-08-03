# Open Rails ShapeTemplate And Road Design/Implementation

## Purpose

This document describes and records a minimal Open Rails implementation for
DynTracks, static TrackObjs, and TSRE Rulers. It covers explicit Open Rails
track-profile selection, opt-in procedural replacement of ordinary static rail
shapes, road-aware DynTrack rendering, and profile-driven Ruler previews.

The original design review compared:

- local Open Rails commit
  `91414172dea8f16c08e587f2792264110aaabab1`;
- upstream `master` commit
  `b5109a8ec55ffcd5f155e49a0b3114815e166480`.

The relevant parsing, profile-loading, wire, and superelevation paths were
materially unchanged between those revisions. The implementation and visual
acceptance described below were made against the local commit. Before an
upstream submission, the six runtime-file changes should be rebased onto the
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
TrackProfiles/default_road.stf -> default_road
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
| Road, no `ShapeTemplate` | `default_road`, then legacy `TrProfileRoad` |
| Road, missing both road defaults | Profile 0 and one useful warning |

An invalid explicit road name also falls back to profile 0. It must not
silently select either implicit road default, because an explicit but invalid
choice should have one deterministic compatibility fallback.

The initial patch should not add a canned road cross-section to Open Rails.
Falling back to profile 0 keeps the object visible and retains existing
behavior. The profile loader accepts traditional `TrProfile*` files plus
TSRE's reserved `default_*` IDs. Routes that need an implicit road default
provide `TrackProfiles/default_road.stf`; `TrProfileRoad.stf` or `.xml`
remains a compatibility fallback.

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

### TSRE Ruler integration

Parse TSRE's `Ruler` world object instead of ignoring it. The object uses the
existing MSTS `Points`/`Point` tokens plus the optional `ShapeTemplate` token.
Its stored points are tile-relative world positions; `Position` normally
duplicates the first point and must not be added to every point again.

Render one independent, locally generated straight profile segment between
each adjacent point pair. Convert MSTS point Z to Open Rails/XNA Z exactly
once, then place the local mesh with a world matrix. This keeps profile
geometry local and compatible with the existing instanced rendering model.

Ruler profile selection is intentionally explicit:

| Ruler state | Behavior |
| --- | --- |
| Valid explicit name | Render with the named profile |
| `ShapeTemplate DEFAULT` | Render with profile 0 |
| No value or `ShapeTemplate DISABLED` | Do not generate a Ruler shape |
| Missing or ambiguous name | Warn and do not generate a Ruler shape |

Rulers are visual guides, not track database objects. They do not query TDB or
RDB, do not generate overhead wire, and do not receive superelevation. Their
profile shapes remain available when global superelevation is disabled.

## Recommended Upstream Patch Organization

Prepare one small pull request with four reviewable commits:

1. **Honor DynTrack ShapeTemplate**
   - add the text token and parsed property;
   - retain profile filename identity;
   - add deterministic named-profile resolution;
   - propagate the selected profile through every DynTrack rendering branch;
   - preserve profile-0 behavior for legacy routes.

2. **Add road DynTrack rendering behavior**
   - classify DynTrack road identity with `0x00000100`;
   - load TSRE `default_*` profile IDs alongside traditional `TrProfile*`;
   - use `default_road`, then `TrProfileRoad`, as the implicit road profile;
   - suppress rail superelevation lookup and overhead wire for roads;
   - retain a visible profile-0 fallback.

3. **Honor ShapeTemplate on safe static rail TrackObjs**
   - parse the optional value on `TrackObj`;
   - use explicit names instead of profile guessing when present;
   - retain all generated zero-roll subsections for an explicit selection;
   - preserve every existing static superelevation exclusion and legacy
     behavior when the value is absent.

4. **Render ShapeTemplate Rulers**
   - parse the TSRE Ruler and its point list;
   - resolve its explicit profile through the shared profile catalog;
   - generate local straight segments between adjacent stored points;
   - keep Rulers independent of TDB/RDB, wire, and superelevation.

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
| Road DynTrack with explicit `default_road*` | Visually accepted | Route `bbb` contains straight/curved plain and marked road objects |
| Road DynTrack without an explicit template | Implemented, not visually tested | Uses `default_road`, then `TrProfileRoad`, then visible profile-0 fallback |
| Mixed rail and road DynTracks | Visually accepted | Rail and road profiles render together in route `bbb` |
| Road DynTrack on an electrified route | Pending visual test | Must confirm no dynamic wire is generated |
| Road and rail records sharing a `UiD` | Pending visual test | Must confirm the road skips TDB superelevation lookup |
| Straight and curved road subsections | Visually accepted, detailed banking review deferred | Basic geometry is correct; road banking deserves a later focused check |
| Road CarSpawner/RDB traversal | Out of rendering-patch scope; pending | Uses the existing independent RDB traffic path |
| Static rail TrackObj without `ShapeTemplate` | Implemented, pending regression test | Retains the existing Open Rails superelevation path |
| Static rail TrackObj with valid explicit name | Visually accepted | Named ORTS profiles render on ordinary TrackObjs in route `bbb` |
| Static rail TrackObj with `DEFAULT` | Implemented, pending visual test | Procedurally replaces the shape with profile 0 |
| Static rail TrackObj with `DISABLED` | Implemented, pending visual test | Retains the original static shape |
| Static rail TrackObj with missing name | Implemented, pending visual test | Retains the original shape and warns once |
| Excluded static shapes | Preserved by implementation, pending regression test | Roads, tunnels, junctions, crossovers, moving tables, and unresolved sections remain static |
| Ruler with named profile | Visually accepted | Profile-driven Ruler geometry renders in route `bbb` |
| Ruler while superelevation is disabled | Implemented | Ruler generation is independent of the superelevation option |
| Multi-point Ruler | Implemented, basic visual acceptance | Each adjacent point pair creates an independent straight profile segment |

Final road acceptance requires a visible and correct road profile, no road
wire, no road superelevation, and deterministic fallback behavior.

## Implementation Status

Implemented and incrementally rebuilt in the local Open Rails worktree through
2026-08-03:

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
- all existing static-track safety exclusions remain in force;
- TSRE Ruler objects, their existing `Points`/`Point` data, and
  `ShapeTemplate` are parsed;
- Rulers render named/default profiles as local straight segments without
  TDB/RDB lookup, overhead wire, or superelevation.

The incremental `RunActivity` build succeeds. Route `bbb` contains rail
DynTracks selecting `TrProfileExampleClean`, `TrProfileExampleClean02`,
`TrProfile_SR_w`, `TrProfile_DB1`, and `TrProfile_DB2f`, plus a `DISABLED`
case. These named rail profiles and the legacy profile-0 behavior were
visually accepted in Open Rails on route `bbb`.

Explicit named-profile replacement of ordinary static rail TrackObjs was also
visually accepted in both TSRE and Open Rails on route `bbb`.

Road profile rendering received basic visual acceptance. Route `bbb`
contains `default_road*` profiles and road DynTracks with `StaticFlags (
00100100 )` selecting plain, marked, and role templates. The compatibility
loader now discovers those TSRE profile IDs directly. Missing road defaults
still use the documented visible profile-0 fallback. A closer examination of
road banking and a dedicated overhead-wire check remain deferred.

Named profile rendering on TSRE Rulers was also visually accepted. This path
remains active independently of the Open Rails superelevation setting.

## Shareable Patch Scope

The functional patch is isolated to these six Open Rails runtime files:

- `Source/Orts.Parsers.Msts/TokenID.cs`;
- `Source/Orts.Formats.Msts/WorldFile.cs`;
- `Source/RunActivity/Viewer3D/DynamicTrack.cs`;
- `Source/RunActivity/Viewer3D/Ruler.cs`;
- `Source/RunActivity/Viewer3D/SuperElevation.cs`;
- `Source/RunActivity/Viewer3D/Scenery.cs`.

At the current local revision, this is 346 inserted lines and 43 deleted lines.
VS Code support files, project-file conversions, generated output, and other
local worktree changes are not part of the feature patch.

Useful proof for reviewers consists of:

1. this design and implementation record;
2. the clean six-file Git patch;
3. the zero-error incremental `RunActivity` build result;
4. a short before/after video or screenshots from route `bbb`;
5. the corresponding `ShapeTemplate` entries in the two tested world tiles.
