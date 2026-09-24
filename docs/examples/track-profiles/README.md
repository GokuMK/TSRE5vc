# Track profile family examples

Each STF file in this directory is one complete profile family. The filename
stem is the persisted `ShapeTemplate` family ID; the `ObjectType` role selects
the member inside that family. Copy a whole file into a route's
`TrackProfiles` directory rather than splitting its role blocks into separate
files.

These examples were copied from the visually tested `bbb` route:

| File | Members | Required textures | Purpose |
| --- | --- | --- | --- |
| `RdProfile.stf` | `ROAD MAIN/SINGLE/LEFT/MIDDLE/RIGHT` | `road.ace` | Compact default road family used by ordinary and Flex Road placement |
| `default_road_marked.stf` | `ROAD MAIN/SINGLE/LEFT/MIDDLE/RIGHT` | `road2lane.ace` | Marked multilane-road family with role-specific UV cropping |
| `TrProfile_NR_Bridge.stf` | `TRACK MAIN/LEFT/MIDDLE/RIGHT` | `DB_Rails1.ace`, `DB_Track1.ace`, `DB_Track1s.ace`, `NR_Ground_y1.ace`, `NR_Railing1.ace`, `NR_RdBridge1.ace` | Extracted bridge family showing outer-side and barrier-free middle members |

The road examples are small enough to use as syntax references. The bridge is
an intentionally realistic example with several materials and distance LODs.
It retains `IncludedShapes ( "NR_Bridge1t100mStrt*" )` because that matcher
describes compatible source shapes; it does not define the family identity.

Textures are not bundled here. Copy profiles only into content installations
that provide the listed textures, or replace their material references.
