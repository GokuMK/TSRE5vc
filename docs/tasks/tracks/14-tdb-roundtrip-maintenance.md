# Task 14 - TDB And Route TSection Round-Trip Maintenance

## Status

Implemented and validated on **2026-10-05**: legacy positive-exponent parsing,
six-significant-digit database saving, and stable item-reference ordering by
path position. Pickup semantics and Open Rails compatibility were reviewed.
The initial baseline findings and intermediate trial below are retained for
comparison. The route-section header issues noted below remain unresolved.

## Scope And Baseline

| Item | Baseline |
| --- | --- |
| Code | TSRE5vc `4ea18af`, branch `review/tdb-roundtrip`, with audit-only additions |
| Content root | `C:/MagiPacks/Microsoft Train Simulator` |
| Corpus | 14 TDBs and 13 existing route `tsection.dat` files |
| Procedure | Two production load/save passes without editing track geometry |
| Source integrity | All 164 inspected database/configuration file hashes unchanged |

The comparison ignores whitespace and distinguishes numeric spelling changes,
decimal differences that preserve float32 values, float32 changes, structural
changes, and item-reference permutations. Six-significant-digit comparison is
an additional MSRE-style precision diagnostic.

## Verdict

**TDB saves do not preserve all input numbers, and repeated saves are not
stable.** All 13 route `tsection.dat` files preserve section and path definition
values. USA2 changes one header value. No source blocks or nonnumeric fields
were lost in this corpus.

## Confirmed Findings

### 1. Positive Exponent Corruption

**Highest priority:** in `EUROPE1/settleca.tdb`, `PickupItem` 325 changes the first
`PickupTrItemData` value:

```text
Source:                  1e+006 (1,000,000)
Loaded/serialized/saved:  7
```

`ParserX::GetNumber` handles an exponent minus sign but does not consume a plus
sign. It then processes the plus as an addition expression, producing `1 + 006`.
`GetNumberInside` repeats this implementation.

Code locations at the reviewed baseline:

- `src/tsre/fileFunctions/ParserX.cpp:375` and `:462`.
- Item call site: `src/tsre/tdb/TRitem.cpp:482`.

### 2. Numeric Drift During Parsing And Subsequent Saves

The parser accumulates each fractional digit into a `float`. Its integer
decimal divisor overflows at ten fractional digits: `10^10` exceeds `int32`.

Example from EUROPE1, `TrackNode` 101, `TrVectorSections` scalar index 1760
(zero based, including the leading count; section 109, `param[15]`):

```text
First save:   0.0011408899
Second save:  0.0011408953
```

The first output itself contains enough fractional digits to overflow the
parser on reload. This changes the numeric value beyond formatting alone.

### 3. Precision Policy Differs Between TDB And Route TSection

`TDB::save` uses **eight significant digits**; `TSectionDAT::saveRoute` uses
**six**. Original 2001 stock-route real literals in this corpus use at most six.

Most stock-route first-save changes disappear when compared at six digits;
the EUROPE1 pickup corruption remains. On the second pass, **688 TDB values
differ even at six significant digits**, excluding item-reference ordering.

Nine-digit captures distinguish loaded float32 values from normal formatting.
Normal eight-digit TDB formatting also changes some nearest-float32 values;
this is not automatically a defect under an MSRE-compatible decimal policy.

Code locations:

- `src/tsre/tdb/TDB.cpp:3669`.
- `src/tsre/tdb/TSectionDAT.cpp:301`.

#### Git History Of The Precision Setting

The eight-digit setting predates TSRE5vc. Local Git history shows no change
from six to eight digits in `TDB::save()`:

| Repository | Commit | Date | Evidence |
| --- | --- | --- | --- |
| TSRE5 | `365409fbc42150fa2c7bcd3bbd544ed669aae617` — `track placement` | 2014-12-06 | Introduces `TDB::save()` with `out.setRealNumberPrecision(8)` already present; its parent has no `TDB::save()` implementation. |
| TSRE5 | `4b59cecd05e4e992c54ff80b93586d60120c5273` — `big update` | 2015-05-14 | Reworks saving and adds `saveEmpty()`, retaining eight digits. This is the commit attributed by current TSRE5 blame. |
| TSRE5 | `a458722` — `flex track math` | 2015-05-19 | Introduces the explicit six-digit setting for route TSection saving. |
| TSRE5vc | `0512891` — `Test TSRE vc` | 2025-11-21 | Initial import already contains the eight-digit TDB setting in `src/TDB.cpp`. |

The 2014 introduction is an ancestor of the current TSRE5 checkout, whose
history is not shallow. The commit message does not explain why eight digits
were chosen. The mismatch with the stock-route six-digit convention is
therefore longstanding behavior, not an identified recent precision-setting
regression. This history check does not establish when the separate parsing
defects or their observed effects began.

### 4. USA2 Route TSection Header Changes

USA2's `TrackSections` header changes **`386` → `384`**. All 244 `TrackSection`
definitions (IDs 376–759, with gaps), their parameters, all `TrackPath` entries,
and `SectionIdx` remain numerically unchanged.

The loader discards the declared `TrackSections` header and derives the next
index from the highest definition. Saving emits the next index minus the
global base. The declared range therefore shrinks by two; no section
definitions are lost. The intended allocation semantics should be confirmed
before changing this behavior.

Code locations: `src/tsre/tdb/TSectionDAT.cpp:312`, `:353`, and `:361`.

### 5. Item-Reference Ordering Changes

Item references reorder in **77 vector-node blocks**. **364 reference
positions** change, preserving exactly the same referenced IDs and
multiplicities. This happens in the full-save sorting step and occurs again
on the second pass.

These permutations are excluded from the numeric-drift counts below.
Equal-distance ordering merits a separate stability review.

Code location: `src/tsre/tdb/TDB.cpp:3556`.

## Results By Route

Pass 1 compares source to first save. Pass 2 compares first save to second save.
Float32 columns compare the nearest float32 values of decimal input and output.
Counts describe fields, not nodes, and exclude ordering changes. Six-digit
columns provide the supplemental MSRE-style precision comparison. The final
column counts reference blocks reordered on the first save.

| Route | Float32 changes, pass 1 | Float32 changes, pass 2 | Six-digit changes, pass 1 | Six-digit changes, pass 2 | Reordered reference blocks |
| --- | ---: | ---: | ---: | ---: | ---: |
| EUROPE1 | 8,155 | 7,264 | 1 | 146 | 12 |
| EUROPE2 | 6,248 | 5,459 | 95 | 267 | 8 |
| JAPAN1 | 14,393 | 12,692 | 0 | 61 | 27 |
| JAPAN2 | 7,311 | 6,813 | 0 | 26 | 0 |
| large | 40 | 39 | 0 | 0 | 0 |
| mini | 0 | 0 | 0 | 0 | 0 |
| procedural | 6 | 4 | 2 | 0 | 0 |
| qttest1 | 0 | 0 | 0 | 0 | 0 |
| slarge | 0 | 0 | 0 | 0 | 0 |
| terrainsize | 14 | 12 | 0 | 0 | 0 |
| TUTORIAL ROUTE | 278 | 236 | 0 | 0 | 0 |
| ularge | 25 | 18 | 2 | 0 | 0 |
| USA1 | 17,624 | 21,720 | 0 | 13 | 0 |
| USA2 | 17,249 | 15,676 | 0 | 175 | 30 |
| **Total** | **71,343** | **69,933** | **100** | **688** | **77** |

- **Route TSection:** 12 of 13 files match numerically after the first save;
  USA2 differs only in the header. All 13 are numerically stable on the second save.
- **TUTORIAL ROUTE:** no input route `tsection.dat`; saving creates an empty one.
- **Empty TDBs:** `mini`, `qttest1`, and `slarge` remain numerically unchanged.

## Follow-Up Priority

1. Positive exponent signs are fixed and regression-tested. Further changes to
   legacy numeric parsing, including long fractional parts, are outside the
   authorized fix; newer parsers exist but are not used here yet.
2. Six-significant-digit saving is now implemented after fixing exponent parsing
   and completing the trial below.
3. Preserve declared route-section ranges where valid.
4. Stable ordering for equal-position item references is now implemented.

## Exponent Fix And Six-Digit Trial

### Retained Change

Only the exponent-sign branches in `ParserX::GetNumber` and
`ParserX::GetNumberInside` changed: both now consume an optional `+` before
reading exponent digits. Fractional accumulation, exponent scaling, units,
addition syntax, and parser selection are unchanged.

The `parser-exponents` suite passes **38/38** checks across both entry points:
positive, negative, unsigned and zero exponents; uppercase `E`; negative
mantissas; units; addition expressions; following values; and preservation of
the closing parenthesis. The existing `tdb-load` suite passes **15/15** checks.

An eight-digit corpus control differs from the original first-save captures
in exactly one TDB field: EUROPE1 pickup item 325 now saves `1000000` instead of
`7`. This isolates the observed effect of the parser fix.

### Trial Setup

For the experiment only, `TDB::save()` and `TDB::saveTit()` used six significant
digits. The audit's direct TDB capture also used six; route TSection saving
already uses six. Both production save cycles ran against the same 14-route
corpus, with outputs redirected to fresh audit directories.

After the trial, the temporary precision changes were reverted and the normal
application rebuilt. The retained production change is only the exponent fix.
The separately named six-digit executable remains available as a local artifact.

### Trial Results

All **14 TDBs are numerically stable on the second six-digit save**, excluding
item-reference permutations. By comparison, the fixed-parser eight-digit
control still changes **69,933** numeric fields on its second save.

The six original stock TDBs in this corpus—EUROPE1, JAPAN1, JAPAN2, TUTORIAL
ROUTE, USA1 and USA2—retain every source numeric value on the first six-digit
save, excluding reference ordering. Existing higher-precision files round on
the first save:

| Route | Source-to-first-save numeric changes | First-to-second-save numeric changes |
| --- | ---: | ---: |
| EUROPE2 | 14,789 | 0 |
| large | 93 | 0 |
| procedural | 26 | 0 |
| terrainsize | 33 | 0 |
| ularge | 55 | 0 |
| All other routes | 0 | 0 |
| **Total** | **14,996** | **0** |

These counts include decimal changes that preserve the nearest float32 value.
Of the first-save differences, 100 differ even from simply formatting the
source at six significant digits: EUROPE2 has 95, procedural has two, and
ularge has three. The unchanged legacy parsing arithmetic remains relevant
at rounding boundaries, despite stable subsequent output.

Route TSection results are unchanged: USA2's header still changes `386` to
`384`, all definitions retain their values, and all 13 files are stable on
the second save. Reference ordering still changes in 77 TDB blocks. Those
behaviors were outside this fix.

All 164 original database/configuration file hashes remain unchanged in both
the eight-digit control and six-digit trial.

### Trial Artifacts

Local artifacts, excluded from version control:

- `build/tdb-audit-exponent-fixed-8/`: control reports, captures and source hashes.
- `build/tdb-audit-exponent-fixed-6/`: six-digit reports, captures and source hashes.
- `build/TSRE5vc-tdb-six-digit.exe`: experimental six-digit build.
- `build/tdb-six-digit-experiment.patch`: temporary TDB/TIT precision changes.
- `build/parser-exponents.log` and `build/tdb-exponent-load.log`: regression results.

## Implemented Six-Digit Saving And Reference Ordering

### Save Precision

`TDB::save()`, `saveTit()`, and `saveEmpty()` now consistently select six
significant digits. This also covers the shared RDB/RIT save paths. Route
TSection saving already used six and is unchanged. The audit's normal TDB
capture uses six; its diagnostic float32 capture retains nine.

### Ordering Review And Fix

The existing comparator already uses `TRitem::getTrackPosition()`, which
returns `TrItemSData`'s first value. Open Rails also describes this field as
distance along the containing track section, measured from its origin.

Every source reference block in the 14-route corpus was already in
nondecreasing distance order. All 77 blocks changed by the old saver retained
the same distance sequence: the changes were permutations among equal-distance
items. No item membership changed.

`sortItemRefs()` now uses `std::stable_sort` with the existing distance
comparator. Items remain ordered by their position along the path, and items
at equal positions retain their incoming relative order. Item IDs do not
override path position or introduce a new tie-breaking convention.

The focused test supplies 40 equal-distance items between deliberately
misordered earlier and later items. It checks ascending position, preservation
of tie order and stability on a second sort.

## Pickup Meaning And Format Review

### TSRE And Original MSTS Evidence

The TDB/TIT payload is:

```text
PickupTrItemData ( <current-content-real> <hex-flags> )
```

TSRE's existing `PickupObj::getPickupContent()` and `setPickupContent()` use
the first value as current pickup content. The properties panel labels it
**Content**. Marking a pickup broken sets that value to zero. The world
object's `PickupCapacity` separately stores configured capacity and fill rate;
its `PickupType` also has a separate infinite-capacity setting.

`TRitem::set()` reads the first value as a real number and the second through
`GetHex()`. Saving writes a real number followed by eight hexadecimal flag
digits. No conversion to an integer field or special fixed-decimal format
is needed.

Original EUROPE1 pickup item 325 contains:

```text
PickupTrItemData ( 1e+006 00000000 )
```

The fixed six-digit saver writes:

```text
PickupTrItemData ( 1e+06 00000000 )
```

Both represent content **1,000,000**, with unchanged zero flags. Other source
items contain quantities such as `200000`, `1000`, and `0`, and flags such as
`00000280` and `00000380`. The flag bit meanings remain unverified and all bits
are preserved.

### Open Rails Comparison

Reviewed the local clean Open Rails `master` at
`8aafb31472d21f40e7f1885d168b0cfddc5142a1` and the official online reader.
Its `PickupItem` processes only `TrItemId`, `TrItemRData`, `TrItemSData`, and
`TrItemPData`. It has no processor for `PickupTrItemData`; the generic block
reader skips that unhandled block. Therefore Open Rails does not use this
quantity or its flags and cannot establish their precise MSTS units.

Open Rails obtains the refilling rate from the world object's `PickupCapacity`.
Its reader converts that separate payload from assumed pounds/pounds per
second into kilograms/kilograms per second. Those assumptions must not be
applied automatically to the ignored TDB content field. No quantity-unit or
flag reinterpretation was introduced in TSRE.

Sources:

- [Open Rails PickupItem reader](https://github.com/openrails/openrails/blob/8aafb31472d21f40e7f1885d168b0cfddc5142a1/Source/Orts.Formats.Msts/TrackDatabaseFile.cs#L1500).
- [Open Rails generic block reader](https://github.com/openrails/openrails/blob/8aafb31472d21f40e7f1885d168b0cfddc5142a1/Source/Orts.Parsers.Msts/STFReader.cs#L1577).
- [Open Rails world pickup capacity](https://github.com/openrails/openrails/blob/8aafb31472d21f40e7f1885d168b0cfddc5142a1/Source/Orts.Formats.Msts/WorldFile.cs#L409).
- [Open Rails diesel refill rate usage](https://github.com/openrails/openrails/blob/8aafb31472d21f40e7f1885d168b0cfddc5142a1/Source/Orts.Simulation/Simulation/RollingStocks/MSTSDieselLocomotive.cs#L1325).
- TSRE: `src/tsre/tdb/TRitem.cpp`, `src/tsre/world/objects/PickupObj.cpp`,
  and `src/routeEditor/properties/PropertiesPickup.cpp`.

The regression suite also saves and reloads content `1000000` with nonzero
flags `00000280`, confirming that six-digit exponent notation preserves both.

## Final Implementation Validation

The final build, with six-digit saving and stable sorting both enabled, passes:

| Check | Result |
| --- | --- |
| Application build | Passed |
| `parser-exponents` | 38/38 |
| `tdb-ordering` including pickup serialization/reload | 4/4 |
| `tdb-load` | 15/15 |
| Python structured-comparator tests | 5/5 |
| `git diff --check` | Passed |
| Original database/configuration hashes | All 164 unchanged |

All **14 TDBs and 14 TIT files are byte-identical between their first and
second saves**. All **1,303** reference blocks are ordered by nondecreasing
path position in the source and both saved generations. No source reference
order changes remain; the former 77-block tie-order churn is eliminated.

The six original stock TDBs retain every source numeric value. The one-time
rounding of higher-precision inputs remains as documented in the trial.
The 13 existing route `tsection.dat` files are also byte-identical between
their first and second saves; USA2's source-to-first-save header difference
remains unchanged.

The expanded byte comparison also identifies a separate missing-file case:
TUTORIAL ROUTE has no source route `tsection.dat`. Its first save creates an
empty file with `TrackSections ( 0 )` and `SectionIdx ( 0 )`; the second save
writes counters `2` and `1`. This is an additional route-section header issue,
outside the precision, ordering and pickup changes implemented here. No
section/path definitions are present or lost in that file.

Final local evidence is in `build/tdb-audit-six-stable/`: `report.json`,
`ordering-check.json`, source hashes, logs, and both capture generations.
Focused regression logs are `build/tdb-ordering-six-stable.log`,
`build/parser-exponents-six-stable.log`, and `build/tdb-load-six-stable.log`.

## Validation And Artifacts

- Application build succeeded.
- Existing `tdb-load` suite: **15/15** checks passed.
- Structured comparator tests: **5/5** passed.
- `git diff --check` passed.

The reusable audit procedure is documented in
[tests/tdb/README.md](../../../tests/tdb/README.md).

Local evidence remains under `build/tdb-audit-20261005-final/`, outside version
control:

| Artifact | Contents |
| --- | --- |
| `report.json` | Field paths, examples, counts, and precision comparisons |
| `source-hashes.json` | SHA-256 baseline for original files |
| Per-route `pass1.log` and `pass2.log` | Production loading and saving logs |
| Per-route `pass1/` and `pass2/` | `raw`, `float32`, and `saved` captures |

The `float32` captures use nine significant digits with the same serializer.
They diagnose formatting differences but cannot independently detect fields
the serializer omits or hardcodes.

## Coverage Limits

This review exercises database and TSection loading/saving without a full
world or route-editor load. Only the installed uncompressed text corpus was
examined.

Existing handling of unused fields, including hardcoded vector field 7 and
skipped `TrEndNode` arguments, remains a separate coverage risk. Neither caused
value loss in this corpus.

## Related Work

- [Track database targeting](06-track-database-targeting.md).
- [Undo for DynTrack TDB and dynamic TSection changes](../editor/02-undo-dyntrack-tdb-tsection.md).
