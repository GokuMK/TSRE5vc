# Task 08 - Open Rails Road DynTrack Compatibility

## Objective

Verify how Open Rails handles a DynTrack world object whose vector data belongs
to RDB, then define the smallest compatible change needed to render it with a
road profile.

This task is suitable for an Open Rails-focused agent, but the initial
cross-source review has already been completed and is recorded here.

## Status

Static source review complete. Runtime fixture and any Open Rails change remain
pending.

No Open Rails source has been modified.

The tracked source baseline reviewed was commit
`91414172dea8f16c08e587f2792264110aaabab1`. The Open Rails working tree already
contained unrelated local changes; these were not modified during this
research.

## Source Review Result

### World loading

`Orts.Formats.Msts.WorldFile.DyntrackObj` parses:

- `SectionIdx`;
- `Elevation`;
- `CollideFlags`;
- `StaticFlags`;
- position/orientation;
- five dynamic track sections.

Unknown tokens are skipped. This suggests a TSRE extension token is unlikely
to crash the Open Rails parser, but native MSTS tolerance must be tested
separately.

### Database selection

Open Rails loads rail TDB and road RDB as separate structures, but the
DynTrack world object has no resolved database owner.

`StaticFlags` is used for general scenery properties such as shadows,
animation, terrain, and global shape lookup. Bit `0x00100000`, used by TSRE's
default DynTrack, is not named in the Open Rails `StaticFlag` enum and is not
used to select RDB.

### Rendering

`Scenery.cs` sends every `DyntrackObj` to:

```text
SuperElevationManager.DecomposeDynamicSuperElevation(...)
```

That function decomposes its five sections and always creates procedural
viewers. It attempts to match sections against the superelevation dictionary,
but that dictionary is built from rail TDB only. If no match is found, it
still generates the mesh without superelevation.

No direct null dereference was found for the normal mixed-route case.

### Profile selection

Open Rails explicitly documents in code:

```text
Dynamic track objects will ALWAYS use TRPIndex = 0
```

Therefore a road DynTrack currently receives:

- `TrProfile.xml`, if present;
- otherwise `TrProfile.stf`;
- otherwise the canned Kuju rail profile.

Additional profiles such as `TrProfileRoad.stf` cannot be selected by a
DynTrack with current code, even though they can participate in matching
replacement profiles for static track shapes.

## Expected Current Behavior

For a valid road DynTrack world object with matching RDB vectors:

- Open Rails should parse the object;
- RDB should load independently;
- the visual DynTrack should be generated;
- it should look like default rail, not road;
- it should not receive rail superelevation unless an unrelated rail section
  accidentally matches the same tile and `UiD`;
- road traffic behavior depends on the RDB path, not the visual profile.

This is a source-based expectation, not yet a runtime result.

## Required Controlled Fixture

Create a small route containing:

- one normal rail DynTrack in TDB;
- one road DynTrack in RDB;
- distinct rail and road default textures;
- a road CarSpawner traversing the road DynTrack;
- at least one straight and one curve;
- unique object `UiD` values initially;
- a second version with matching rail/road `UiD` values to test accidental
  superelevation matching;
- optional `TrProfile.stf` and `TrProfileRoad.stf`.

Retain:

- `.w` records;
- route `tsection.dat`;
- `.tdb`, `.rdb`, `.tit`, and `.rit`;
- TSRE log;
- OpenRailsLog;
- screenshots in TSRE, MSTS, and Open Rails.

Run:

1. TSRE save/reload.
2. Native MSTS route load.
3. Native MSTS TDB/RDB rebuild where safe.
4. Open Rails route load with no custom profiles.
5. Open Rails with both rail and road profiles.
6. A road vehicle pass over the generated RDB section.

## Compatibility Questions To Answer

- Which native `StaticFlags` bit identifies rail versus road DynTrack?
- Does MSTS accept road DynTrack at all, and does rebuild retain it?
- Does Open Rails log a TDB/RDB/world-object mismatch?
- Does a road DynTrack render once, disappear, or render as rail?
- Can same-`UiD` rail/RDB objects cross-match in the superelevation dictionary?
- Does CarSpawner traverse the new road vector normally?
- Does Open Rails ignore a TSRE `ShapeTemplate` token safely?
- Does native MSTS ignore that token safely?

## Proposed Open Rails Design

Open Rails needs a database/network resolver for DynTrack before profile
selection.

Minimal compatible approach:

1. Decode the confirmed native MSTS rail/road flag from `StaticFlags`.
2. Add `TrackNetworkKind` or equivalent to the parsed DynTrack object.
3. Keep rail DynTrack on profile index `0` for compatibility.
4. For road DynTrack, select a named/default road profile:
   - preferably `TrProfileRoad`;
   - otherwise a profile explicitly marked for roads;
   - otherwise a built-in simple road profile;
   - finally fall back to profile `0` with a warning.
5. Skip wire and rail superelevation lookup for road DynTrack.

If TSRE writes a compatible `ShapeTemplate` extension and Open Rails accepts
it, an enhanced resolver may use:

1. explicit profile file stem/name;
2. network default;
3. legacy profile `0`.

Do not make Open Rails understand TSRE's full advanced template format. It
only needs a stable mapping to its own track profiles.

## Alternative Without Open Rails Changes

A mixed route cannot reliably display both rail and road DynTracks correctly
with current Open Rails behavior because all DynTracks use profile `0`.

Possible content-only workarounds are limited:

- use a road profile as `TrProfile` and make every DynTrack look like road;
- convert road DynTracks to static shapes after editing;
- avoid road DynTrack in exported Open Rails content.

None satisfies the intended mixed rail/road workflow. An Open Rails code
change or accepted extension is required for full compatibility.

## Open Rails Profile References

- Local source guide:
  `Source/Documentation/Online/How to Provide Track Profiles for Open Rails Dynamic Track.docm`
- [Official current profile guide](https://static.openrails.org/files/OpenRails-Testing-How%20to%20Provide%20Track%20Profiles%20for%20Open%20Rails%20Dynamic%20Track.pdf)
- [Open Rails manual: track profiles and superelevation](https://open-rails.readthedocs.io/en/latest/options.html#superelevation)

## Deliverables For An Open Rails Agent

Report:

1. exact Open Rails commit reviewed;
2. runtime results for every fixture variant;
3. relevant OpenRailsLog warnings/exceptions;
4. screenshots or a concise visual comparison;
5. confirmed `StaticFlags` interpretation;
6. proposed parsed data field and profile-selection API;
7. whether the change belongs upstream or should remain a TSRE compatibility
   patch;
8. automated tests that can be added to Open Rails.

Do not start implementation until the fixture confirms the native flag rule.

## Acceptance Criteria

- Current Open Rails behavior is verified at runtime, not inferred only from
  source.
- A road DynTrack cannot accidentally use rail wire/superelevation logic.
- Rail DynTrack behavior remains backward compatible.
- Mixed rail and road DynTracks can select different profiles in one route.
- Missing road profiles fall back visibly and log one useful warning.
- The profile selection rule can be shared with TSRE without requiring both
  projects to share generator code.
