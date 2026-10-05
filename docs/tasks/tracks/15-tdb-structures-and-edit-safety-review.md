# Task 15 - TDB Structures And Edit Safety Review

## Status And Scope

Review completed on **2026-10-05**. **No production code or tests changed.**

- TSRE5vc baseline: `c92b0cf`, branch `review/tdb-roundtrip`, including the
  merge of remote main and [Task 14](14-tdb-roundtrip-maintenance.md).
- Open Rails reference: local clean `openrails` clone at
  `8aafb31472d21f40e7f1885d168b0cfddc5142a1`.
- Reviewed node representation, both text serialization paths, placement,
  append/join/reverse/split/delete, item migration, save-time compaction,
  copying, update notifications, and the corresponding editor commands.
- This is a static review with two existing test suites run. It does not
  reproduce a supplied user route or establish the cause of an individual
  disappearing-section report. No route content was edited.

**Recommendation:** introduce small named value structs, but first add editing
regressions and fix the confirmed memory-safety defects below in separate
changes. A field rename alone will not fix the dangerous editing paths.
Open Rails identifies most fields, but does **not** provide authoritative
names for every serialized value.

## 1. Proposed Data Representation

Current definitions are in [TRnode.h](../../../src/tsre/tdb/TRnode.h):
`UiD[12]`, `TRSect::param[16]`, `args[3]`, and the parallel
`TrPinS[3]` / `TrPinK[3]` arrays. All UiD and vector-section values currently
use `float`, including identifiers and tile coordinates.

Use simple value types such as `TrackNodeUid`, `TrackVectorSection`,
`TrackPin`, and `JunctionData`. Keep geometry as float for this refactor;
changing the geometry math to double would be a separate project. Use signed
integer tile coordinates and endpoint/pin values, and exact integer IDs.
For IDs read as unsigned by OR, a 32-bit unsigned serialized value is
appropriate, but validate conversion at TSRE's existing signed map/API
boundaries. Do not silently reinterpret its `-1` sentinels.

The tables below use zero-based indices. `ax`, `ay`, `az` mean angles around
the serialized X/Y/Z axes, in radians. These names avoid introducing a new
pitch/yaw/roll convention during a storage refactor.

### UiD: All 12 Fields

Source: OR's actual [UiD parser](https://github.com/openrails/openrails/blob/8aafb31472d21f40e7f1885d168b0cfddc5142a1/Source/Orts.Formats.Msts/TrackDatabaseFile.cs#L508).

| Index | Proposed member | Type | OR evidence / meaning |
| --- | --- | --- | --- |
| 0 | `worldTileX` | integer | `WorldTileX`: world-object owning tile |
| 1 | `worldTileZ` | integer | `WorldTileZ`: world-object owning tile |
| 2 | `worldObjectId` | integer ID | `WorldId`: object's UiD within that world tile |
| 3 | `worldEndpointIndex` (provisional) | integer | OR reads and discards it; TSRE stores an entry from `ends[]` |
| 4 | `tileX` | integer | `TileX`: node location tile |
| 5 | `tileZ` | integer | `TileZ`: node location tile |
| 6 | `x` | float | `X`: local position |
| 7 | `y` | float | `Y`: local height |
| 8 | `z` | float | `Z`: local position |
| 9 | `ax` | float | `AX` |
| 10 | `ay` | float | `AY` |
| 11 | `az` | float | `AZ` |

World ownership and geometric location are distinct. A track's endpoint may
lie in another tile from the world object that owns it. Never combine these
two tile pairs into one field or use the node number as the world-object ID.

OR's separate `UiD(TrVectorSection)` convenience constructor synthesizes some
fields; it is **not** the schema for reading an existing UiD record.

### TrVectorSections: All 16 Fields

Source: OR's [TrVectorSection parser](https://github.com/openrails/openrails/blob/8aafb31472d21f40e7f1885d168b0cfddc5142a1/Source/Orts.Formats.Msts/TrackDatabaseFile.cs#L819).

| Index | Proposed member | Type | OR evidence / meaning |
| --- | --- | --- | --- |
| 0 | `sectionIndex` | integer ID | `SectionIndex`: TSection definition |
| 1 | `shapeIndex` | integer ID | `ShapeIndex`: TrackShape definition |
| 2 | `worldTileX` | integer | `WFNameX`: owning world tile |
| 3 | `worldTileZ` | integer | `WFNameZ`: owning world tile |
| 4 | `worldObjectId` | integer ID | `WorldFileUiD` |
| 5 | `startEndpointIndex` (provisional) | integer | OR `Flag1`; TSRE `ends[0]` |
| 6 | `endEndpointIndex` (provisional) | integer | OR `Flag2`; TSRE `ends[1]` |
| 7 | `unknownToken7` | opaque token pending review | OR skips a string, commonly `00` |
| 8 | `tileX` | integer | `TileX`: section start location tile |
| 9 | `tileZ` | integer | `TileZ`: section start location tile |
| 10 | `x` | float | `X`: section start local position |
| 11 | `y` | float | `Y`: section start local height |
| 12 | `z` | float | `Z`: section start local position |
| 13 | `ax` | float | `AX`: section start orientation |
| 14 | `ay` | float | `AY` |
| 15 | `az` | float | `AZ` |

> ?? Double 00 usually mean that it is short binary Flags field, but indeed, not confirmed.

OR explicitly describes Flag1/Flag2 as incompletely understood; its names
are placeholders, not proof that these are independent bit flags. TSRE
assigns endpoint numbers in `newTrack`/`appendTrack`, copies them into UiD[3],
and swaps them when reversing a vector. This supports the provisional
endpoint names, but does not settle all native MSTS semantics.

> ?? Indeed, those represents "endpoints" of placed TrackShape.

The current loaders parse slot 7 as a float, while both writers replace it
with literal `00`. Do not invent a bitmask interpretation. Separately decide
how to preserve a non-default input token; changing this behavior should be
an explicit compatibility fix with a fixture, not an unnoticed rename.

### Pins, Junctions, Counts, And Node Type

| Current member | Proposed representation | Evidence / caveat |
| --- | --- | --- |
| `TrPinS[i]` | `pins[i].link` | OR `TrPin.Link`: referenced track-node ID |
| `TrPinK[i]` | `pins[i].direction` | OR `TrPin.Direction`: connection direction; keep integer semantics |
| `TrP1`, `TrP2` | `inputPinCount`, `outputPinCount` | OR `Inpins`, `Outpins` |
| `args[0]` on junction | `unknown0` | OR skips the first junction token |
| `args[1]` on junction | `shapeIndex` | OR `TrJunctionNode.ShapeIndex`; TSRE uses it for shape-ID repair |
| `args[2]` on junction | `unknown2` | OR skips the remainder; no established field name |
| `args[0]` on end node | separate `endNodeValue` pending review | Serialized in `TrEndNode`; TSRE currently discards it on load |
| `typ` | explicit node-kind enum | Existing values: invalid -1, end 0, vector 1, junction 2 |
| `iTrv`, `trVectorSection` | owned vector of `TrackVectorSection` | Derive count from container in a later ownership change |
| `iTri`, `trItemRef` | owned vector of item IDs | Order is path position, not item-ID order |

See OR's [TrackNode and pin parser](https://github.com/openrails/openrails/blob/8aafb31472d21f40e7f1885d168b0cfddc5142a1/Source/Orts.Formats.Msts/TrackDatabaseFile.cs#L342)
and [junction parser](https://github.com/openrails/openrails/blob/8aafb31472d21f40e7f1885d168b0cfddc5142a1/Source/Orts.Formats.Msts/TrackDatabaseFile.cs#L584).
Do **not** name `args[0]` after OR's `Idx`: that property is assigned from the
enclosing node index, not this token. Likewise, OR's runtime `SelectedRoute`
does not establish a meaning for serialized `args[2]`.

### Questions For A Native MSTS Review

An MSTS-specific review would help with these remaining fields. No external
MSTS review was performed during this task; the questions are ready for it:

1. Is UiD[3] precisely the world TrackShape endpoint index? How are internal
   subsection boundaries, junctions, and multi-path shapes numbered?
2. Are vector slots 5/6 endpoint indices in that same namespace for every
   shape, including reversed curves and crossovers?
3. What is slot 7, what nonzero values exist, and is its syntax hexadecimal,
   decimal, or an opaque two-character field?
4. What are the first and third `TrJunctionNode` values, and the
   `TrEndNode` value? Which must round-trip even when the editor ignores them?

The known fields can be refactored without waiting for these answers. Keep
unknown values explicit and preserved rather than giving them false names.

### Refactor Boundaries

Direct representation consumers found under `src` are `TDB.cpp`,
`TRnode.cpp/.h`, `TRitem.cpp`, `Ruch.cpp`, and `tests/TestRunner.cpp`.
The work must also preserve indirect users such as undo and `TDBClient`.

- Update **both** loaders (`TDB::loadUtf16Data`, `TRnode::loadUtf16Data`) and
  **both** serializers (`TDB::saveToStream`, `TRnode::saveToStream`). The
  latter pair also serves individual node updates, not just file saving.
- Replace numeric-index loops and `memcpy` of float arrays with member
  copies. Do not retain a union/reinterpret-cast view of a mixed int/float
  struct; that would defeat the type distinction and risk layout errors.
- Math currently passes `param + 13` as a three-float frame. Supply an
  explicit frame conversion or a real three-component value; pointer
  arithmetic across separately declared members is not a safe replacement.
- Preserve TDB/editor Z sign conversions at their existing boundaries.
  Preserve angle composition; do not replace transported frames with
  independently adjusted Euler angles while renaming storage.
- Keep six significant digits for real values. Read and write identifiers
  as integers, without passing through `ParserX::GetNumber`'s float result.
  Use a narrow typed reader at this boundary; do not switch the whole TDB
  to the newer parser or redesign the legacy parser as part of this task.
- Prefer a separate ownership change to `std::vector` and paired pins in
  `std::array`. It removes manual resize/count management but still needs
  explicit graph-edit logic, copy/undo checks, and deletion-lifetime fixes.

## 2. Confirmed Defects And Risks

Line numbers below refer to baseline `c92b0cf`.
P1 means fix before relying on editing/refactor tests; P2 means a correctness
or robustness issue with a more restricted trigger. Static defects are
distinguished from unverified explanations of the reported symptom.

### B1 - P1: Deleting A Vector Uses Its Freed Node

**Confirmed static lifetime defect.**
[TDB.cpp](../../../src/tsre/tdb/TDB.cpp), lines 1403-1431 and 2028-2047.

`deleteVectorSection(int)` deletes `trackNodes[id]` at line 1410, but keeps
using `vect->TrPinS[]` to delete/null endpoint entries and send updates.
Those reads are use-after-free. If freed storage changes, the derived node
IDs can target a different map entry; a crash is also possible.

The ordinary world-object removal path has a second lifetime error:
`removeTrackFromTDB` keeps local `n` and evaluates `j < n->iTrv` after
`deleteFromVectorSection` deletes a one-section vector and returns false.
It does not break or reacquire the node.

This path is reachable from normal `Route::deleteObj`, not only repair
commands. It is a credible cause of apparently unrelated damage, but that
specific outcome has not been reproduced here.

**Repair direction:** capture endpoint IDs before destruction, detach and
validate links using live objects, destroy once, and make callers stop
using deleted nodes. Test isolated vectors, junction-to-end vectors, and
vectors whose two ends attach to the same junction.

### B2 - P1: Array Allocations Are Freed With Scalar Delete

**Confirmed static allocation/deallocation mismatch.** `TDB.cpp`:

| Operation | Incorrect delete lines |
| --- | --- |
| `appendTrack` | 785 |
| `joinVectorSections` | 1144, 1145 |
| `splitVectorSection` | 1333 |
| `deleteFromVectorSection` | 1578 |

These pointers hold `new TRSect[...]` arrays. `TRnode::~TRnode` correctly
uses `delete[]`, but resizing paths use scalar `delete`. This is undefined
behavior even though `TRSect` currently contains only trivial floats and a
particular allocator may appear to tolerate it. Heap damage could surface
during a later, unrelated edit. Do not claim it necessarily corrupts memory
on every run.

**Repair direction:** correct ownership before adding nontrivial fields;
eventually use a container. Also audit join cleanup: removed node pointers
are nulled without destruction and `moveItemsFrom2to1` overwrites an owned
item-reference array without releasing it. Simply adding node deletion to
the existing join would encounter the already-freed section pointer; treat
the ownership transfer as one operation.

### B3 - P1: Removing The First Item Reads Before The Reference Array

**Confirmed static bounds defect.** `TDB.cpp`, lines 1478-1490.

When deleting a vector's first section, an item before the new beginning is
removed by `deleteTrItem`. The loop decrements `i`, then unconditionally calls
`updateTrItem(vect->trItemRef[i])`. Removing the item at index zero reads
`trItemRef[-1]`. At later indices it updates the previous item rather than
the removed/current one.

The base update method being empty does not make this valid C++. In
`TDBClient`, a nonnegative garbage ID can also become an update for the wrong
item. `deleteTrItem` already sends the removed item's update.

**Repair direction:** retain the actual item ID and separate the deleted and
surviving branches; continue at the correct new array index after removal.
Test first, middle, last, and all items removed, plus empty arrays.

### B4 - P2: Duplicate Item References Break Removal Sizing

**Confirmed conditional defect for duplicate/invalid input.** `TDB.cpp`,
`findTrItemNodeIds` at 2512, `deleteTrItem` at 3342-3347, and
`deleteItemFromTrNode` at 3361-3378.

The remover allocates `count - 1` slots but skips every occurrence of the
requested ID. With duplicate references it leaves uninitialized slots.
`findTrItemNodeIds` adds the same node once for each occurrence, so the
remover may immediately run again on that damaged list. For an ID absent
from a multi-item list, it copies `count` entries into `count - 1` slots.
The one-item branch also clears the list without checking the requested ID.

Example requiring a regression: node references `[A, A, B]`, then delete A.
This can become an out-of-bounds write on the second removal. The normal
unique-reference case does not exercise this defect. The loader and
`addItemToTrNode` do not enforce uniqueness.

**Repair direction:** remove by actual match count, visit each owning node
once, and reject/report invalid references before mutation or sorting.

### B5 - P2: Float Storage And Formatting Can Change Integer Identity

**Confirmed representation defect outside the audited stock value range.**
See the float loads and stores in `TRnode.cpp` lines 81-87 / 133-139 and
the matching bulk loader and writers in `TDB.cpp`.

Identifiers in these arrays are currently formatted as real numbers.
At six significant digits, integer ID `1234567` formats to a value equivalent
to `1234570`. Independently, float32 cannot distinguish `16777217` from
`16777216`. Thus merely changing the struct member type while still parsing
through float does not preserve identity.

Wrong section/shape IDs can select missing or different geometry; wrong
world-object IDs can lose or misdirect ownership lookup. This is **not**
evidence that the Task 14 stock corpus contained such IDs or that its
six-digit geometry decision was wrong. Integers need exact serialization;
real literals retain six-digit formatting.

**Regression:** large section/shape/world IDs, negative tile coordinates,
both serialization paths, copy, and reload; check IDs exactly, not with the
six-significant-digit real-number comparison.

### B6 - P2: Join/Split Preconditions And Failure Propagation Are Incomplete

**Confirmed missing guards; valid ordinary calls constrain reachability.**
`TDB.cpp`, lines 1042-1091, 1099-1156, and 1220-1339.

- If none of the four endpoint comparisons matches, `joinVectorSections`
  still concatenates the vectors and removes the presumed joining ends.
  Normal `joinTracks` first finds matching ends, so a direct call with
  unrelated vectors is a regression/robustness case, not proof that normal
  autojoin currently selects geographically remote vectors.
- `joinTracks` discards negative join results and returns success-like zero.
  Placement also does not propagate those failures into its final result.
- `splitVectorSection` assumes `0 < j < count`, valid nodes, reciprocal
  links, and usable definitions. It repins the far endpoint before building
  the replacement graph. Its present deletion caller supplies an interior
  index, but the function itself does not enforce the contract.

**Repair direction:** validate the complete edit before mutation, make
failure results unambiguous, and leave the graph unchanged on failure.
Test missing reverse-section definitions and failed joins as well as valid
joins in all four endpoint orientations.

### B7 - P2: Different Shape Path Starts Can Be Classified As One Junction

**Confirmed key collision; affected real shape not identified.**
`TDB.cpp`, lines 1915-1935.

`placeTrack` converts each start position into the integer key
`x * 100000 + y * 1000 + z * 10`. This is not a unique position key.
For example, `(0, 1, 0)` and `(0, 0, 100)` both produce 1000.
Distinct paths with such starts are treated as sharing a junction before
placement. Truncation also conflates sufficiently close positions.

**Repair direction:** compare position tuples using an explicit geometric
tolerance, or use a tuple key with a documented quantization policy. Test
the collision example and actual coincident starts. This concerns topology
inside a placed shape; it is not a demonstrated explanation of remote loss.

### B8 - P2: Saving After Deleting Every Node Reports One Phantom Node

**Confirmed static empty-state defect.** `TDB.cpp`, `deleteNulls` at
3526-3553, `findBiggest` at 3588-3593, and `saveToStream` at 3801.

`findBiggest()` returns 1 when no live nodes remain. For a previously
nonempty database with all nodes removed, compaction consequently leaves
`iTRnodes == 1`. Serialization emits `TrackNodes ( 1 ... )` without a node
record. The new-empty-database path is different and does not prove this
edit-to-empty transition works.

**Repair direction:** represent zero live nodes correctly and test deleting
the last vector through ordinary removal, then full save/reload for TDB/RDB.

## 3. Additional Investigation And Symptom Triage

These observations should guide tests and reproduction; they are not all
confirmed causes of disappearing geometry.

- **Command scope matters.** `PropertiesTrackObj.cpp` lines 469-480 exposes
  “Remove TDB Vector” and “Remove TDB Tree”. The former removes the whole
  vector containing a matching world object, potentially many track shapes;
  the latter traverses its connected component, subject to a 1000-node
  limit. That can reach geographically distant track by design. Distinguish
  these commands from ordinary object deletion when collecting a report.
- **Both databases are probed.** `Route.cpp` lines 2165-2197 / 2254-2261
  invokes removal in both RDB and TDB. This is already addressed as a
  targeting concern in [Task 06](06-track-database-targeting.md). A valid
  world-object key includes tile coordinates and UiD; equal UiDs alone do
  not imply a collision. Test duplicated/stale ownership across databases.
- **Save changes IDs globally.** `deleteNulls` moves the highest live node
  into a hole and updates pins and signal junction references. A distant
  node's changed numeric ID is not evidence its sections disappeared.
  Compare geometry and world ownership as well as IDs. Check consumers that
  retain node IDs across save/undo and the collaborative editing path.
- **Autojoin is position-based.** `TRnode::equals` / `equalsIgnoreType`
  accepts same-tile locations within 0.17 m without a heading test;
  `joinTracks` selects the first matching node in ID order. Overlaid tracks,
  multiple nearby ends, and tile-boundary equivalents need explicit tests
  before changing the established snapping rule.
- **Item update notifications are incomplete.** Join item translation
  (`moveItemsFrom2to1`) and split item translation alter item positions but
  do not call `updateTrItem` for those changes. Node updates alone contain
  references, not the changed item records. Review client/server replay
  separately; no live collaborative session was exercised here.
- **Prepending to an item-bearing vector:** `appendTrack` can insert at
  index zero but does not shift existing item distances by the new length.
  Its current production caller builds a fresh shape before joining, so
  this is an API-contract gap rather than an established normal UI trigger.
- **Malformed graph handling:** pin loaders can write beyond the three-pin
  arrays if the input counts exceed three; tree traversal indexes pin IDs
  without validating bounds or node existence. Item sorting dereferences
  missing items and assumes usable finite distances. Validation should
  diagnose invalid input before mutation, rather than silently deleting
  unrelated records to make it load.
- **Unknown token preservation:** end-node values are skipped on load and
  vector slot 7 is overwritten with `00` on save. Keep these distinct from
  the verified stock-corpus numeric results of Task 14.

For a reported failure, retain before/after databases and world files, the
exact command and selected `(database, world tile, UiD)`, and whether the
loss appeared immediately, after undo, after save/reload, or on another
client. Distinguish missing world mesh, missing TDB subsection, changed node
ID, broken graph connection, and misplaced track item. Those symptoms have
different causes.

## 4. Existing Validation Performed

The installed Qt/MinGW environment was configured with repository root and
`build` on PATH, both Qt plugin paths, and `QT_QPA_PLATFORM=offscreen`.

| Check | Result on reviewed baseline |
| --- | --- |
| Explicit CMake build of `TSRE5vc` | Ninja: no work to do; executable current |
| `TSRE5vc.exe --test --test-suite flex-point` | **49 passed, 1 failed**, reproduced on two runs |
| Failing case | `complete TDB subsection frames` |
| `TSRE5vc.exe --test --test-suite tdb-ordering` | **4 passed, 0 failed** |

The failing case spans placement, subsection sampling, generated line points,
and reversal (`TestRunner.cpp` lines 1373-1614). Its single final assertion
does not identify which subcheck failed. This review does not attribute the
failure to the merge, precision change, or a particular geometry function.
Disaggregate it and establish a passing baseline before the struct change.
The synthetic tests log a missing `/GLOBAL/tsection.dat` while supplying
their own definitions; the reported result is a completed assertion failure,
not a Qt plugin failure or stalled process.

Existing coverage includes geometry/frame reversal in `flex-point`, stable
item ordering/pickup formatting in `tdb-ordering`, load/save guards in
`tdb-load`, and Task 14's no-edit corpus audit. Searching the test sources
found no direct join/split/delete regression coverage. Passing a no-edit
round trip does not demonstrate safe graph editing.

## 5. Required Editing Regression Plan

Use small in-memory graphs with explicit section definitions and known world
ownership. Call production editing functions. Include a disconnected
sentinel network with its own geometry and items in **every destructive
editing scenario**; snapshot it before the edit and assert it is unchanged.
Run both rail and road variants where behavior is shared.

| Area | Required cases and meaningful assertions |
| --- | --- |
| Named fields | Distinct values in every field; both load/save paths agree; exact IDs, signed tiles, real precision, unknown tokens and copy semantics |
| Append | Both directions; straight/curve, slope/bank, tile crossing; item distance unchanged for append and increased by new length for prepend |
| Join | End/start, start/end, start/start, end/end; correct order/reversal, length sum, reciprocal pins, exact section ownership multiset, item translation and direction |
| Failed join | Unrelated ends, missing node/definition, missing reverse definition, same vector, occupied junction; explicit result and unchanged graph |
| Split | Every interior boundary of a multi-section vector; left/right geometry union equals original, correct new ends, reciprocal links; invalid boundary rejected |
| Split items | Before/on/after cut, equal-distance ties, multiple item types; current rule places an item exactly at cut on the right at distance zero |
| Delete subsection | First/middle/last/only section; same world object occupying consecutive and separated subsections; remove all matching pieces and no others |
| Delete items | First/middle/last/all references removed; empty list, duplicate/missing references; no negative index or unrelated item update |
| Junctions and loops | Zero/one/multiple remaining branches, both vector ends attached to one junction, multi-path shape, disconnected sentinel; correct surviving links and counts |
| Reverse | Two reversals restore world-space path, metadata and item direction; sample `s` before versus `length - s` after; unequal curves, slope/bank, tile boundaries |
| Save/reload | Sparse IDs, distant node moved by compaction, signals referencing moved junction, all nodes deleted; graph equivalence independent of renumbering |
| Undo/copy | Snapshot then edit; original snapshot unchanged; undo restores geometry, ownership, items, pins and database identity; no aliased owned arrays |
| Notification replay | Record changed/deleted nodes and items, replay to a second database, compare full graph and item distances after join/split/delete |
| Placement identity | Colliding linear position keys, coincident starts, close parallel endpoints, world tile different from geometry tile, large IDs |
| Explicit broad deletion | Whole-vector/tree commands remove exactly their documented connected scope; ordinary deletion remains limited to selected world ownership |

After each successful operation, check active pin counts and reciprocal
links, positive section counts for live vectors, resolvable section/shape
IDs, valid item ownership and distance ranges, and preservation of all
unselected section records. Account for paired track items through their
documented ownership rules. Check world-space samples independently of the
array indices being refactored.

Before save, verify that reference ordering meets the chosen API contract.
After full save/reload, require nondecreasing path distance and stable ties.
Do not require item IDs to be numerically increasing or assume reversal
already sorts the in-memory reference array.

Add deterministic sequences such as split/rejoin, reverse twice,
delete/undo, and repeated edit/save/reload. Use a memory-checking build for
B1-B4 where supported; a passing ordinary run cannot prove undefined
behavior absent. Keep geometry tolerance explicit and do not apply it to
identifiers, connectivity, record counts, or ownership.

## 6. Suggested Implementation Sequence

1. Isolate the current `flex-point` failure; add focused editing fixtures and
   regressions for B1-B4, including the disconnected sentinel network.
2. Fix deletion lifetimes, array ownership, and item-removal indexing in
   separately reviewable commits. Address the empty-save transition.
3. Introduce named fields and exact integer I/O. Update every loader,
   serializer, math boundary, copy path, and direct consumer together.
   Preserve established geometry and six-digit real formatting.
4. Convert owned arrays/counts and parallel pins to simple containers in a
   separate step. Define join/split failure contracts and update replay.
5. Address the remaining topology/identity issues with their own fixtures;
   resolve the native MSTS unknowns before semantic renaming of those fields.
6. Run all targeted editing and geometry tests, then repeat Task 14's
   two-pass official-installation audit using output copies. Add edited
   fixtures because that corpus audit alone does not exercise mutation.

Acceptance requires an explained and passing baseline, no memory-safety failures
in the edit regressions, unchanged unrelated networks, exact identifier and
ownership preservation, valid topology through undo/save/reload, and no
unexplained numeric or structural regression against Task 14. This review
does not implement or claim completion of those repairs.
