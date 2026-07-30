# Task 08 - Open Rails ShapeTemplate And Road DynTrack Compatibility

## Objective

First, make Open Rails honor the `ShapeTemplate` name saved by TSRE on
DynTrack world objects and select the matching Open Rails track profile.

After that small interoperable milestone, verify how Open Rails handles a
DynTrack world object whose vector data belongs to RDB and define the smallest
road-aware change.

This task is suitable for an Open Rails-focused agent, but the initial
cross-source review has already been completed and is recorded here.

## Status

Static source review complete. Milestone A (`ShapeTemplate` profile selection)
is ready after Task 07 can write and preview ORTS profiles. The road-database
fixture and road-aware behavior are a later milestone.

No Open Rails source has been modified.

The tracked source baseline reviewed was commit
`91414172dea8f16c08e587f2792264110aaabab1`. The Open Rails working tree is an
intentional local variant adapted to compile under VS Code rather than the
official Visual Studio setup. Preserve these local changes. They cannot be
pushed upstream and were not modified during this research.

## Source Review Result

### World loading

`Orts.Formats.Msts.WorldFile.DyntrackObj` parses:

- `SectionIdx`;
- `Elevation`;
- `CollideFlags`;
- `StaticFlags`;
- position/orientation;
- five dynamic track sections.

Unknown tokens are skipped. MSTS also ignores undefined tokens, so TSRE can
store `ShapeTemplate` directly without a sidecar.

### Database selection

Open Rails loads rail TDB and road RDB as separate structures, but the
DynTrack world object has no resolved database owner.

`StaticFlags` is used for general scenery properties such as shadows,
animation, terrain, and global shape lookup. The Open Rails `StaticFlag` enum
documents shadow flags, `Terrain`, `Animate`, and `Global`, but does not name
the TrackObj `0x80`/`0x100` bits or DynTrack's `0x00100000`.

Open Rails treats every `TrackObj` as global scenery regardless of the
`Global` bit. Wire and superelevation code classify static track through
`TrackShape.RoadShape`, not the candidate `StaticFlags` database bit. DynTrack
likewise has no resolved TDB/RDB owner.

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

## Milestone A - Honor ShapeTemplate

Extend the parsed Open Rails DynTrack object with the optional
`ShapeTemplate` string already used by TSRE.

Selection behavior:

1. if `ShapeTemplate` is present and matches an ORTS track profile, use it;
2. if it is absent, retain the existing DynTrack profile-index-0 behavior;
3. if it is present but cannot be resolved, warn once and retain the existing
   behavior.

Do not add special prefixes, source-qualified names, or a mandatory road
profile filename. The saved `ShapeTemplate` value is the profile name to
resolve. File stem and declared profile name may both be accepted
case-insensitively when unambiguous.

Initial fixture:

- place several rail DynTracks in TSRE;
- assign different ORTS profiles through `ShapeTemplate`;
- save and reload in TSRE;
- load the route in the local Open Rails build;
- verify each DynTrack uses the selected profile;
- remove `ShapeTemplate` from one object and verify original Open Rails
  guessing/profile-0 behavior remains unchanged.

Milestone A acceptance:

- TSRE-selected ORTS profiles display correctly on multiple DynTracks in one
  Open Rails route;
- legacy routes without `ShapeTemplate` render exactly as before;
- an invalid name stays visible through the original fallback and produces one
  useful warning.

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

The existing Ruler **Create Road Paths** command may be used to bootstrap a
reference RDB straight. It is not the test object for this task: Ruler writes
RDB vectors owned by a TSRE-only Ruler and does not write corresponding
`Dyntrack` world records. The fixture must additionally contain real DynTrack
world objects whose vectors are in RDB.

Retain:

- `.w` records;
- route `tsection.dat`;
- `.tdb`, `.rdb`, `.tit`, and `.rit`;
- TSRE log;
- OpenRailsLog;
- screenshots in TSRE and Open Rails, plus MSTS only if a usable installation
  later becomes available.

Run:

1. TSRE save/reload.
2. Open Rails route load with no custom profiles.
3. Open Rails with both rail and road profiles.
4. A road vehicle pass over the generated RDB section.
5. Optionally load/rebuild in native MSTS if a usable editor installation
   becomes available; this is not an acceptance gate.

## Compatibility Questions To Answer

- Which explicit TSRE database marker does Task 06 write, and does Open Rails
  read it reliably?
- Does Open Rails log a TDB/RDB/world-object mismatch?
- Does a road DynTrack render once, disappear, or render as rail?
- Can same-`UiD` rail/RDB objects cross-match in the superelevation dictionary?
- Does CarSpawner traverse the new road vector normally?
- Does Open Rails resolve a TSRE `ShapeTemplate` token as designed?
- Does the Ruler-generated RDB reference load cleanly despite having no
  corresponding DynTrack world object, and how does that differ in logs from
  the real road DynTrack?

## Milestone B - Road DynTrack Design

Open Rails needs a database/network resolver for DynTrack before profile
selection.

Minimal compatible approach:

1. Read the explicit rail/road marker established by Task 06; also decode a
   native `StaticFlags` rule later if reliable evidence is found.
2. Add `TrackNetworkKind` or equivalent to the parsed DynTrack object.
3. Keep rail DynTrack on profile index `0` for compatibility.
4. Apply milestone A's explicit `ShapeTemplate` selection before any default.
5. For road DynTrack without an explicit template, select a configured or
   built-in road default, finally falling back visibly to profile `0` with a
   warning.
6. Skip wire and rail superelevation lookup for road DynTrack.

Do not make Open Rails understand TSRE's full advanced template format. It
only resolves the `ShapeTemplate` name against its own track profiles.

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
2. the preserved local VS Code build modifications;
3. milestone A parsing, selection, fallback, and runtime results;
4. relevant OpenRailsLog warnings/exceptions;
5. screenshots or a concise multi-profile visual comparison;
6. later road-fixture results and any supported `StaticFlags` interpretation;
7. whether each change belongs upstream or should remain a local TSRE
   compatibility patch;
8. automated tests that can be added to Open Rails.

Milestone A does not depend on a native road flag and may be implemented
before the road fixture. Do not guess a road `StaticFlags` value in milestone
B.

## Acceptance Criteria

- Open Rails reads TSRE's DynTrack `ShapeTemplate` token.
- A valid name selects the matching ORTS profile without special naming
  syntax.
- Missing or invalid values retain original Open Rails profile-selection
  behavior.
- Current Open Rails behavior is verified at runtime, not inferred only from
  source.
- A road DynTrack cannot accidentally use rail wire/superelevation logic.
- Rail DynTrack behavior remains backward compatible.
- Mixed rail and road DynTracks can select different profiles in one route.
- Missing road profiles fall back visibly and log one useful warning.
- The profile selection rule can be shared with TSRE without requiring both
  projects to share generator code.
