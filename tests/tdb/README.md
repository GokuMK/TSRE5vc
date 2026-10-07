# TDB and route TSection round-trip audit

Build `TSRE5vc`, configure the Qt/MinGW runtime environment, and run:

```powershell
python tests/tdb/audit_roundtrip.py --exe build/TSRE5vc.exe --root "C:/MagiPacks/Microsoft Train Simulator" --output build/tdb-audit
python -m unittest discover -s tests/tdb -p "test_*.py"
```

The output directory must not exist. The audit enumerates immediate route
directories containing a TDB. It loads the real global/route TSection, TDB and TIT
with automatic section-ID repair disabled, captures the production serializers,
then calls the complete `TDB::save()` path in a fresh output tree. It repeats the
load/save on the saved copy. Original database/configuration hashes are checked
afterward. TUTORIAL ROUTE may have no source route `tsection.dat`; this is reported
explicitly. Generated output stays under the requested output directory.

`report.json` compares source to serialization, serialization to full save,
source to full save, and first to second save. Blocks match by node/item/section
ID; keyword case and whitespace are ignored. Numeric spelling, decimal changes
that preserve the nearest float32, changes to float32, missing blocks and text
changes are reported separately. Item-reference permutations are identified by
multiset equality. Six-significant-digit comparison is an additional diagnostic,
not a blanket tolerance or a guarantee of MSRE behavior. No differences are
silently accepted as harmless. A successful capture exit status means the audit
ran; fidelity failures remain in its report.

A separate nine-significant-digit capture (`float32`) preserves the serializer's
stored float values. `source_to_float32` and `float32_to_raw` help distinguish
parser changes from decimal precision lost during normal formatting. This uses
the same serializer and does not independently detect fields it hardcodes or
omits; the source comparisons remain necessary.

This initial harness supports uncompressed UTF-16 or UTF-8 SIMIS text, one TDB
per route and ordinary quoted strings. It is intended for controlled local
corpora, not arbitrary malformed inputs or general SIMIS expression syntax.
It exercises database loading/saving without starting the full route editor,
loading world tiles or modifying track geometry.

For one route, use the underlying capture suite:

```powershell
build/TSRE5vc.exe --test --test-suite tdb-roundtrip --game-root "C:/content" --route EUROPE1 --test-cases build/capture-europe1
```

The focused legacy exponent regression suite runs without route content:

```powershell
build/TSRE5vc.exe --test --test-suite parser-exponents
build/TSRE5vc.exe --test --test-suite tdb-ordering
```

It covers both `ParserX::GetNumber` entry points, exponent signs and case,
units, addition, and the reader position after a number. It does not replace
the legacy parser or change its fractional arithmetic.

`tdb-ordering` verifies sorting by path distance, stable ties, repeat sorting,
and six-digit scientific serialization/reloading of pickup content with
hexadecimal flags. Normal TDB/TIT saving uses six significant digits.

Task 15 editing and typed-field regressions run without installed route content:

```powershell
build/TSRE5vc.exe --test --test-suite tdb-editing
build/TSRE5vc.exe --test --test-suite tdb-fields
build/TSRE5vc.exe --test --test-suite tdb-load
build/TSRE5vc.exe --test --test-suite flex-point
```

`tdb-editing` covers both rail and road joins in all orientations, append and
prepend, split/rejoin, item migration, selected-world-object deletion,
junctions/loops, duplicate and absent item references, production undo, reversal
and reloading edited graphs. Each fixture includes a disconnected sentinel
network and item. It does not run editor UI commands, multiplayer replay or
save-time compaction on the edited fixtures.

`tdb-fields` exercises both node and database load/save paths, distinct values
in all UID/vector slots, signed tiles, large exact integers, older scientific
integer notation, hexadecimal bytes, end-node values, optional junction
metadata and deep copies. Real fields still use the legacy parser and stream
precision. These tests are ordinary builds, not a substitute for a memory
sanitizer run.
