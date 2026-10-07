# Task 15 - TDB Structures And Edit Safety Review

## Status And Scope

Initial review completed on **2026-10-05** without code changes. The conservative
implementation began on **2026-10-06**, on `feature/tdb-edit-safety` based on
`9cf4e4a`. The original review below remains a historical record and backlog;
see the implementation record at the end for delivered scope and validation.

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

### Conservative Implementation Scope - 2026-10-06

The user prefers preserving established, user-tested behavior. The next work
should therefore be **tests, confirmed critical fixes, then named structs**.
This narrower scope supersedes the broader improvement suggestions below;
the original findings remain a backlog, not an instruction to implement all
of them. This update changes the plan only, not production code or tests.

**Baseline reconciliation completed, 2026-10-06:** main `117f3e3` lacked
`6919c50` (legacy exponent fix, six-digit saves, stable item ordering, and
Task 14 audit tests/document). At the user's request, that commit was applied
without conflicts as **`9cf4e4a`** on main. This restores the accepted Task 14
baseline; it does not implement the Task 15 refactor or critical fixes.

The build passed, as did 38 exponent checks, 4 ordering/pickup checks, 15
load checks and 5 Python audit tests. `flex-point` remains at 49/50 with the
previously reported `complete TDB subsection frames` failure.
The new audit is in `build/tdb-audit-main-20261006`: all 164 source hashes
remained unchanged, and all 14 saved TDBs and 14 TITs are byte-stable between
passes. Compared with the previous audit, only the procedural route's
metrics differ; its source TDB and route tsection had changed since that
audit. Other route metrics match. The known Task 14 tsection exceptions
remain outside this integration.

1. Recheck the current baseline (main has advanced since the original
   review), isolate the previously failing `flex-point` case, and add direct
   join/split/delete/undo regressions. Verify a disconnected network stays
   unchanged. Establish expected results independently of the old code's
   output so characterization does not accidentally bless a known defect.
2. Fix the demonstrated lifetime, allocation and indexing defects B1-B3
   with the smallest changes. Add the duplicate-reference regression for
   B4 and fix its memory-safety failure without redesigning item validation.
   Keep each correction separate from the representation change.
3. Replace numeric field indexing with small named value structs, updating
   both I/O paths and their consumers. Keep the existing owned arrays,
   counts, node maps and editing algorithms for this pass. Exact integer
   fields and the correctly typed hexadecimal byte are part of this work;
   six-digit formatting remains for real values.

The [native MSTS review](../../msts/msts-tdb-fields-and-msre-save-review.md)
provides these useful corrections to the original review:

- Vector slot 7 is a **single hexadecimal byte**, retained by MSTS. Use
  `uint8_t opaqueByte`, with a nonzero round-trip fixture; its bit meanings
  remain unknown. Parsing `10` must yield `0x10`, and saving must emit two
  hexadecimal digits rather than overwrite the field with `00`.
- Endpoint values are retained unsigned integer editor metadata. The user's
  TrackShape endpoint interpretation remains supported; the complete native
  numbering rule has not been independently established.
- Unknown junction values and the scalar end-node value are retained by
  MSTS. Preserve them as integers without assigning speculative semantics.
  Preserving the existing scalar end-node field is a focused I/O correction
  with its own fixture. New `TrEndLinkFile` support is deferred.
- Native vector section/shape ID narrowing is a compatibility limitation,
  not a reason to truncate TSRE IDs. Preserve TSRE's full integer values.
- The native save ordering findings do **not** establish a defect in
  TSRE's current compaction policy. Keep that policy and ascending output
  block order. Do not introduce native-style pointer links or renumbering.

Defer container/ownership redesign, autojoin heuristics, geometric position
keys, broad failure/transaction handling, notification redesign, save
performance work, and other robustness improvements. B8's edit-to-empty
count issue can have a separate reproducer and focused fix; it is not a
prerequisite for renaming fields. Revisit deferred behavior only with a
specific failing case and a clear expected outcome. The test matrix below
is a regression backlog; writing a test does not itself authorize changing
an established behavior.

## 1. Proposed Data Representation

Definitions at the original review baseline were in [TRnode.h](../../../src/tsre/tdb/TRnode.h):
`UiD[12]`, `TRSect::param[16]`, `args[3]`, and the parallel
`TrPinS[3]` / `TrPinK[3]` arrays. All UiD and vector-section values currently
use `float`, including identifiers and tile coordinates.

Use simple value types such as `TrackNodeUid`, `TrackVectorSection`,
`TrackPin`, and `JunctionData`. Keep geometry as float for this refactor;
changing the geometry math to double would be a separate project. Use signed
integer tile coordinates and pin values, unsigned integer endpoint metadata,
and exact integer IDs.
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
| 3 | `worldEndpointIndex` | unsigned integer | Native retained metadata; TSRE stores a TrackShape endpoint from `ends[]` |
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
| 5 | `startEndpointIndex` | unsigned integer | OR `Flag1`; TSRE `ends[0]`; native retained metadata |
| 6 | `endEndpointIndex` | unsigned integer | OR `Flag2`; TSRE `ends[1]`; native retained metadata |
| 7 | `opaqueByte` | `uint8_t` | Native one-byte hexadecimal I/O; meanings of bits unresolved |
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

The reviewed loaders parse slot 7 as a float, while both writers replace it
with literal `00`. The native review now establishes the required one-byte
hexadecimal representation. Correct preservation is an explicit I/O fix
with a nonzero fixture, not an unnoticed rename. Do not invent bit names.

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

The following questions were prepared for the native review. Its report now
answers the syntax and retention questions; semantic gaps remain as noted:

1. Is UiD[3] precisely the world TrackShape endpoint index? How are internal
   subsection boundaries, junctions, and multi-path shapes numbered?
2. Are vector slots 5/6 endpoint indices in that same namespace for every
   shape, including reversed curves and crossovers?
3. Slot 7 is now confirmed as one retained hexadecimal byte. Its bit meanings
   and the behavioral validity of arbitrary nonzero values remain unknown.
4. What are the first and third `TrJunctionNode` values, and the
   `TrEndNode` value? Native retention is now confirmed; preserve these
   values, while leaving the unresolved meanings explicit.

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
- Defer conversion of owned arrays/counts to containers. Pairing each pin's
  link/direction in a small struct can preserve the existing fixed capacity
  and ordering; it does not require new ownership or editing algorithms.

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

### CMK Large-Route Check - 2026-10-06

At the user's request, tested `C:/trainsim/routes/CMK` with the current
`9cf4e4a` executable. The build was current. Used the production
`tdb-roundtrip` suite for two load/save passes into separate output copies,
plus the existing structured comparator and an additional graph/identity
comparison. Local artifacts and logs are in
`build/tdb-audit-cmk-20261006`; the local driver is
`build/audit_cmk_20261006.py`.

**Result: no structural loss or connection/identity changes observed.**

| Check | Result |
| --- | --- |
| Production capture, including load and save | Both passes succeeded, approximately 3.4 / 3.3 seconds |
| Nodes | 8,590 preserved: 4,898 vector, 3,052 junction, 640 end |
| Vector subsections | 35,936 preserved |
| Item records / vector item references | 21,539 / 20,269 preserved |
| Node IDs, item IDs, pins and directions | Exact structural signatures unchanged |
| Section IDs, shape IDs, world ownership, endpoint metadata and geometry tiles | Exact structural signatures unchanged |
| Per-node item ownership and integer node metadata | Exact structural signatures unchanged |
| Missing pin targets, nonreciprocal pin pairs, missing item targets | Zero in source and both saved copies |
| Added/removed blocks, atom-count changes, nonnumeric changes | None in TDB, TIT or route tsection comparisons |
| Item reference permutations | None |
| Route `tsection.dat` | All 18,189 parsed atoms unchanged from source |
| First versus second full save | TDB, TIT and route tsection each byte-identical |
| Source integrity | All 30 hashed database/configuration files unchanged, including RDB/RIT |

The first save changes higher-precision real values, as expected for this
TSRE-produced route. Maximum observed component changes are 0.0049 m for
node coordinates, 0.005 m for section/item coordinates, 0.05 m for item
distance along the path, and 0.000005 radians for node/section angles.
Integer identity and connection fields do not change.

Do not describe every numeric difference as ideal decimal rounding alone:
4,753 TDB values still differ under the comparator's six-significant-digit
check. The nine-digit capture shows some differences already arise while
loading into float32, before applying six-digit output. For example,
`0.0026192318` saves as `0.00261924`. These small numeric effects settle after
the first save; they do not produce continuing drift on the second pass.
The TDB's raw six-digit serialization and full save also match exactly at
the parsed-atom level.

This check exercises rail TDB/TIT and shared route track definitions, without
track editing or loading world scenery. RDB/RIT were integrity-hashed, not
round-tripped. It is not a join/split/delete test or a native MSRE live test.
No production code or source route content was changed for this check.

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

Follow the narrower three-step scope recorded above: establish regression
coverage, fix confirmed critical defects, then introduce named fields and
typed I/O. Keep those changes separately reviewable. The other findings
remain deferred unless a specific reproducer justifies a focused repair.

At each step, run the relevant editing and geometry tests. After the
representation change, repeat Task 14's two-pass official-installation audit
using output copies. Include edited fixtures because the no-edit corpus
audit alone does not exercise mutation. Account explicitly for intended
preservation fixes to byte, integer and end-node metadata values.

Acceptance requires an explained and passing baseline, no memory-safety failures
in the edit regressions, unchanged unrelated networks, exact identifier and
ownership preservation, valid topology through undo/save/reload, and no
unexplained numeric or structural regression against Task 14. This review
does not implement or claim completion of those repairs.


## 7. Conservative Implementation - 2026-10-06

Implemented on `feature/tdb-edit-safety`, based on `9cf4e4a`. No route source
files were edited. The implementation follows the agreed order: regression
fixtures, focused critical fixes, then named fields. The implementation was
prepared for commit at the user's request on 2026-10-07.

### Confirmed Defects Fixed

- **B1:** replace five scalar deletes of vector-section arrays with `delete[]`
  in append, join, split and subsection deletion.
- **B2:** retain endpoint IDs before deleting a vector node; stop the selected
  world-object removal loop when that vector has been deleted. No access to
  the freed vector is needed for endpoint cleanup or update notifications.
- **B3:** after deleting an item from a section's head, continue with the
  updated reference array rather than using the decremented index (`-1` when
  the first reference was removed).
- **B4:** count and remove every matching reference, allocate the actual
  remaining size, leave absent references alone, and deduplicate the list of
  owning nodes before removal. The validation API still reports duplicate
  ownership as before.

The initial 34 editing checks gave **26 passes / 8 failures** before these
fixes and **34 / 0** afterward, before changing the representation. The final
suite adds junction loops, prepend, edited reloads, repeated world ownership
and an item on the disconnected sentinel network. This reproduces concrete
faulty editing behavior; it does not establish that every reported disappearing
section had the same cause.

### Named Fields And I/O

[TrackNodeData.h](../../../src/tsre/tdb/TrackNodeData.h) defines `TrackNodeUid`,
`TrackVectorSection`, `TrackPin` and `JunctionData`. `TRnode` retains its owned
arrays, counts and existing node type; no map/container or algorithm redesign
was introduced. Consumers in TDB, TRitem, Ruch, Path and SignalObj use the named
fields. Geometry retains the existing axis and angle conventions.

Both node and database text paths share the field readers/writers:

- Signed tile coordinates, unsigned section/shape/world IDs and endpoint
  metadata are read without conversion through float32. Older scientific
  integer literals are accepted. Existing signed node-map keys and item
  references remain signed integers; this does not expand every downstream
  identifier representation in TSRE.
- Vector `opaqueByte` is a retained `uint8_t`, read as hexadecimal and written
  as two lowercase hexadecimal digits. Its meanings are deliberately unnamed.
- Junction metadata remains opaque unsigned integers; an omitted third value
  defaults to zero. End nodes retain their own unsigned scalar instead of
  silently saving zero. `TrEndLinkFile` remains unsupported in this change.
- Real coordinates and angles retain the legacy parser and six-digit saving.
  Narrow, local integer adapters are used; no newer parser migration or broad
  legacy-parser rewrite was performed.
- Invalid integer ranges fail loading and leave the existing database protected
  against overwrite. Deep copies retain all fields without aliasing arrays.

### Validation And Limits

Final sequential build passed. Focused validation:

| Suite | Passed / failed |
| --- | --- |
| `tdb-editing` | 48 / 0 |
| `tdb-fields` | 40 / 0 |
| `flex-point` | 50 / 0 |
| `parser-exponents` | 38 / 0 |
| `tdb-ordering` | 4 / 0 |
| `tdb-load` | 17 / 0 |
| Python audit unit tests | 5 / 0 |

That is **197 C++ checks and 5 Python tests**, all passing. Final C++ logs are
`build/task15-final-<suite>.log`; baseline/fix-stage editing logs are
`build/tdb-editing-before-fixes.log` and `build/tdb-editing-after-fixes.log`.
`git diff --check` passes. The source diff was also reviewed with mechanical
field substitutions factored out, to isolate behavior changes from renaming.

The old `flex-point` failure was a test expectation error: the final 4.32589 m
curve is tessellated into equal intervals, so its second point is about
2.162945 m along the curve, not 4 m. The corrected assertion samples every
emitted point at its actual subdivision distance. Production geometry was not
changed for this correction; all **50** cases pass.

CMK was rerun in `build/tdb-audit-cmk-task15-20261006`. Its first saved TDB, TIT
and route tsection are **byte-identical to the pre-refactor baseline**, and
all comparison metrics and structural signatures match. Both saves are stable.
The 8,590 nodes, 35,936 vector sections, 21,539 items, 20,269 references, pin
connectivity, ownership and metadata are preserved, with zero missing node/item
targets or nonreciprocal pin pairs. All 30 source hashes remain unchanged.

The official-installation audit completed separately in
`build/tdb-audit-task15-serial-20261006`: **14 routes**, **164 unchanged source
hashes**, and exactly the same comparison metrics as the Task 14 baseline.
All **382** compared capture/output data files (including copied metadata)
are byte-identical to their baseline counterparts. TDB/TIT second saves remain
stable; the previously documented USA2/TUTORIAL route-tsection exceptions
remain unchanged.

An initial concurrent stock/CMK audit exhausted available memory in Python's
comparison step. The user confirmed concurrent system tasks were consuming
memory; rerunning the stock audit separately succeeded. The final rebuild and
focused test suites were also run sequentially.

Ordinary regression builds were used; no AddressSanitizer/Valgrind result is
claimed. Tests exercise production database operations and undo, but not live
GUI commands, multiplayer notification replay or native MSRE. CMK's RDB/RIT
were hashed, not round-tripped. Edited fixtures use stream save/reload; the
real-route audits exercise full saves. Broad invalid-graph handling, placement
key collisions, prepend item policy, compaction policy and the edit-to-empty
count defect remain deferred. The broader matrix in section 5 remains a
backlog and is not claimed fully implemented.
