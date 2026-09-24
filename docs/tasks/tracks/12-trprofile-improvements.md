# Task 12 - TrProfile Families and Object Types

## Objective

Extend the route-local ORTS profile catalog so one STF file can describe a
track, road, or static-object profile family. A family may contain role
variants used automatically for multi-path static TrackObjs and grouped Flex
placement.

## Status

Implemented in TSRE on the `feature/trprofile-improvements` branch. Open Rails
support is a separate follow-up task.

## File Discovery

TSRE reads every `.stf` and `.xml` file in the route `TrackProfiles` directory.
The old `TrProfile*` filename restriction no longer applies. If both formats
have the same file stem, XML wins as before.

STF supports multiple top-level profile blocks without a wrapper:

```text
SIMISA@@@@@@@@@@JINX0p0t______

TrProfile (
    ObjectType ( ROAD MAIN )
    ...
)

TrProfile (
    ObjectType ( ROAD LEFT )
    ...
)
```

XML remains one `TrProfile` per file. Supporting several XML profiles would
require an agreed wrapper format and is intentionally deferred.

## ObjectType

The first value identifies the consumer:

- `TRACK` - rail TrackObjs and DynTracks;
- `ROAD` - road TrackObjs and DynTracks;
- `STATIC` - non-track procedural objects such as Rulers.

The optional second value identifies a family role:

- `MAIN` - selectable family profile and automatic role source;
- `SINGLE` - selectable standalone profile, reused without substitution;
- `LEFT`, `MIDDLE`, `RIGHT` - automatically selected family members.

`ObjectType ( ROAD )` is equivalent to `ObjectType ( ROAD MAIN )`. A missing
`ObjectType` retains ORTS compatibility and means `TRACK MAIN`.

Object property panels use two selectors. The first lists the profile family;
the second lists every subtype actually present in that family (`MAIN`,
`SINGLE`, `LEFT`, `MIDDLE`, or `RIGHT`). Selecting a subtype stores its exact
synthesized profile ID. Native TSRE templates and special values such as
`NOT SET`, `DEFAULT`, and `DISABLED` do not have a subtype.

Flex placement deliberately retains its single profile selector. It presents
only `MAIN` and `SINGLE`; grouped placement chooses the directional roles
automatically.

## Identity and Persistence

For this milestone the file stem is the family ID. `Name` is descriptive and
is not an alias or identity. Catalog and persisted `ShapeTemplate` IDs are:

```text
RdProfile          -> MAIN
RdProfile_single   -> SINGLE
RdProfile_left     -> LEFT
RdProfile_middle   -> MIDDLE
RdProfile_right    -> RIGHT
```

The object type is part of the in-memory catalog key, so a file may contain,
for example, both `TRACK MAIN` and `ROAD MAIN`. Callers already know whether
they are rendering track, road, or static geometry and use that type to
disambiguate the shared external ID.

An STF family may contain at most one profile for each exact object-type/role
pair. The first occurrence wins and later duplicates are ignored with a
diagnostic. This makes accidental duplicates deterministic without silently
changing an established family member.

The conventional default road family is `RdProfile`. The experimental
`default_road` and `TrProfileRoad` fallback names are not retained as implicit
defaults.

## Multi-path Resolution

When a static TrackObj selects a `MAIN` profile:

- consecutive paths with the same directed starting rotation form one group;
- one path in a group uses `MAIN`;
- the first path in a group uses `LEFT`;
- the last path in a group uses `RIGHT`;
- paths between them use `MIDDLE`;
- a missing role falls back to `MAIN`.

Starting rotations are compared modulo 360 degrees with a 0.1-degree
tolerance. They are deliberately not folded modulo 180 degrees because a
reversed path also reverses its local left/right meaning. This makes crossing
and junction shapes resolve each road independently; for example,
`Road2LCross.s` becomes `LEFT, RIGHT, LEFT, RIGHT`.

When `SINGLE` is selected, every path uses that same profile. Selecting an
explicit role ID also keeps that role for every path.

The same metadata-driven family lookup is used by track and road Flex groups.
Native TSRE templates retain their existing suffix lookup.

## Compatibility

- Existing untyped ORTS rail profiles remain `TRACK MAIN` and keep their file
  stem IDs.
- Old separate road profiles need `ObjectType ( ROAD ... )` metadata or
  migration into an `RdProfile.stf` family.
- Profiles intended for Rulers need `ObjectType ( STATIC ... )`.
- World-file `ShapeTemplate` syntax is unchanged.
- No TDB, RDB, TrackShape, or DynTrack format change is introduced.

## Verification

The `orts-profile` suite covers:

- missing metadata defaulting to `TRACK MAIN`;
- STF and XML ObjectType parsing;
- arbitrary profile filenames;
- several top-level STF profiles;
- synthesized family role IDs;
- type-filtered and selectable-only catalogs;
- property-selector family and subtype catalogs;
- static multi-path `LEFT/MIDDLE/RIGHT` resolution within directed-yaw groups;
- two-path crossing groups resolving as `LEFT/RIGHT, LEFT/RIGHT`;
- missing-role fallback to `MAIN`;
- `SINGLE` reuse on all paths;
- first-wins duplicate handling and diagnostics;
- same-stem XML precedence without `Name` aliases.

Current automated result:

```text
[tests:orts-profile] cases=29 passed=29 failed=0
[tests:dyntrack-road] cases=7 passed=7 failed=0
```

The incremental application build also succeeds.

### Route `bbb` fixture migration

The route-level test data uses the family format rather than keeping one file
per role:

- `RdProfile.stf` contains road `MAIN`, `SINGLE`, `LEFT`, `MIDDLE`, and
  `RIGHT`;
- `default_road_marked.stf` contains the corresponding marked-road family;
- `TrProfile_DB1_cut2`, `cut4`, `cut6`, and `ramp1` each contain track
  `MAIN`, `LEFT`, `MIDDLE`, and `RIGHT` members. The cut middle profiles retain
  the shared deck/track span without either side wall, walkway, or railing;
  the ramp middle retains its common structure without either side walkway or
  railing.
- `TrProfile_NR_Bridge` contains `MAIN`, `LEFT`, `MIDDLE`, and `RIGHT` members.
  Its source-specific `IncludedShapes` matcher is retained, while its family
  ID and display names no longer expose the original 100-metre shape name.
  Side roles retain only their outer sidewalk and railing, while `MIDDLE`
  omits both sidewalks and railings but keeps the lower bridge support.

The former `_lft`/`_rgt` world references were migrated to the standardized
`_left`/`_right` catalog IDs. The redundant split member files and the old
`default_road` duplicate were removed after references were migrated to
`RdProfile`.

## Follow-up

- Implement the same family discovery, ObjectType parsing, and multi-path
  resolution in Open Rails.
- Agree on a wrapper before allowing multiple profiles in one XML file.
- Add a future per-profile name/ID field only if families need more than one
  member with the same object type and role.
