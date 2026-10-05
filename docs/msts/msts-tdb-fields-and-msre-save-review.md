# MSTS TDB fields and MSRE save-time node numbering

> Mirror of `reports/msts-tdb-fields-and-msre-save-review.md` in the separate
> MSTS research workspace. Analysis scripts, disassembly, audit JSON and
> proprietary fixtures referenced below are not bundled in TSRE.

Review date: **2026-10-05**. Static review of the official-update executable
and MSTS Bin 1.8, prompted by TSRE's
`docs/tasks/tracks/15-tdb-structures-and-edit-safety-review.md`.

## Results and limits

- **The `00` field is one hexadecimal byte.** MSTS loads, retains and saves
  it. It is neither a float nor a 16-bit field. No individual bit meaning or
  rendering/editing decision based on it was established in this review.
- Several values ignored by other readers are **editor metadata retained by
  MSTS**, not necessarily disposable fields. The endpoint-related values are
  among them. Junction and end-node values also round-trip natively.
- **The inspected node container is not a spatially paged TDB.** It is a
  contiguous array of pointers, with a 1,024-entry growth increment. Pins
  become pointers after loading.
- MSRE's apparent “slide all later node IDs down” is implemented by assigning
  dense IDs for serialization in existing slot order. It does **not** require
  physically sliding all node objects and repeatedly remapping integer links.
- **The reported TSRE-to-MSRE save crash is not yet explained.** No failing
  route or crash instruction was available. This review does not demonstrate
  that last-node-to-hole compaction is intrinsically incompatible with MSTS,
  nor prove that every editor subsystem tolerates every graph permutation.

Only Linux-side static inspection and a read-only local file census were run.
No Windows access, Wine execution, application patch or route edit was performed.
The original analysis and evidence remain in the MSTS workspace. At the user's
request, this report is also mirrored into TSRE's `docs/msts`; no TSRE code or
Task 15 content was changed.

## Evidence and versions

| Specimen | SHA-256 |
| --- | --- |
| Official update, `proprietary/extracted/official-update/train.exe` | `730b5054adc73c2cbfb0b3eb6eb2d9d95ae339fcc3922fe74cb318d747319584` |
| Bin 1.8, `proprietary/msts_app/train-1.8.exe` | `69218fce876298c684a2140c7d3925a452c47bb10037ffd8c491f65c5c0c6e7a` |

All **29 bounded code ranges** in the audit are byte-identical between these
two specimens. This is a claim about those ranges, not every TDB/editor routine
or the whole executables. Addresses below are VAs; image base is `0x00400000`.

Reproduce the comparisons, field census and instruction listings with:

```sh
python3 scripts/audit_msts_tdb.py
```

Outputs:

- Audit hashes and corpus counts (MSTS workspace: `analysis/pe/tdb-audit.json`).
- Direct disassembly (MSTS workspace: `analysis/pe/tdb-core-disassembly.txt`).
- Reader/writer decompilation (MSTS workspace: `analysis/pe/ghidra-tdb-schema-review.txt`).
- Container, numbering and identity helpers (MSTS workspace: `analysis/pe/ghidra-tdb-save-and-identity-review.txt`).
- Junction and allocation helpers (MSTS workspace: `analysis/pe/ghidra-tdb-editor-fields-review.txt`).
- Byte readers (MSTS workspace: `analysis/pe/ghidra-tdb-byte-io-review.txt`) and
  actual byte writers (MSTS workspace: `analysis/pe/ghidra-tdb-byte-writers-review.txt`).
- Save coordinator (MSTS workspace: `analysis/pe/ghidra-tdb-save-callers.txt`).

The decompiler misses register arguments and suppresses some live pin-reading
instructions as “unreachable.” Field sizes, byte I/O, node allocation and save
numbering were checked against instructions; decompiled prototypes alone are
not a reliable schema. The first byte-I/O export also contains two exploratory
functions which turned out not to be the byte writers; use the separate
byte-writers export for those.

TSRE observations below refer to the locally supplied `src/tsre/tdb/TDB.cpp`,
`TRnode.cpp` and Task 15. Local Git HEAD is `123bccc` on `main`; the supplied
Task 15 separately names `c92b0cf` as its review baseline (that commit is not
resolvable in this clone). The audit records current source-file hashes to
avoid conflating those two histories. TSRE observations are not descriptions
of MSTS behavior.

## Native token names

These are full native IDs, not Open Rails' historical flattened enum values.
The field numbers in subsequent tables are zero-based positions, not token IDs.

| Symbolic token | Native ID |
| --- | --- |
| `TrackDB` | `0x000404D0` |
| `TrackNodes` | `0x0004007D` |
| `TrackNode` | `0x0004007E` |
| `TrPins` | `0x0004007F` |
| `TrPin` | `0x00040080` |
| `TrVectorNode` | `0x00040081` |
| `TrJunctionNode` | `0x00040082` |
| `TrEndNode` | `0x00040086` |
| `TrEndLinkFile` | `0x00040088` |
| `TrVectorSections` | `0x00040089` |
| `UiD` | `0x0004006C` |

## `TrVectorSections`: what the executable actually stores

The block contains a count followed by fixed records of 16 textual values.
The native runtime record occupies **64 bytes**, but that is an internal layout,
not a 64-byte serialized record: it includes an editor-metadata pointer, padding
and derived geometry values.

Reader: `0x005CB5C8`. Writer: `0x005CBA5E`.

| Field | Meaning / proposed name | Serialized type and native handling |
| --- | --- | --- |
| 0 | Section definition ID | Unsigned 32-bit input; retained as `uint16` at record `+0x00`. Loader also checks the input against the section-table count. |
| 1 | TrackShape definition ID | Unsigned 32-bit input; retained as `uint16` at `+0x02`. |
| 2, 3 | Owning world tile X/Z | Signed 32-bit input, then **narrowed through signed 16-bit values** and stored sign-extended at `+0x08/+0x0C`. |
| 4 | World object's UiD | Unsigned 32-bit at `+0x10`. This is not a TDB node ID. |
| 5, 6 | Endpoint-related indices | Two unsigned 32-bit values, retained in editor-only metadata. See below; do not treat them as booleans. |
| 7 | `opaqueByte` — normally `00` | **One byte**, represented by two hexadecimal digits in text; stored at `+0x14`. Preserved by the native writer. Bit meanings unresolved. |
| 8, 9 | Geometry tile X/Z | Signed 32-bit values read directly into `+0x18/+0x1C`; unlike the ownership helper, this path does not perform the signed-16 narrowing. |
| 10–12 | Local X/Y/Z | Three float32 values at `+0x20…+0x28`. |
| 13–15 | Orientation about X/Y/Z | Three float32 values at `+0x2C…+0x34`. Keep existing coordinate/angle conventions. |

The ID narrowing is a native implementation restriction, **not a recommendation
to truncate TSRE's own IDs**. Store exact integers in TSRE and diagnose values
outside the intended MSTS compatibility profile. Similarly, distinguish the
serialized signed-32 tile type from the narrower ownership values MSTS retains.

### The `00` byte

The reader calls the generic byte-sequence reader with **length 1** at
`0x005CB73B`; the resulting byte is stored at `0x005CB95A`. The writer passes
the address of record `+0x14` and length 1 at `0x005CBBA5…0x005CBBBF`.

The relevant I/O vtable entries resolve to:

| Operation | Unicode/text implementation | Binary implementation |
| --- | --- | --- |
| Read bytes, vtable `+0x20` | `0x00692350`: consumes pairs of hexadecimal digits | `0x006943B0`: reads the requested byte count |
| Write bytes, vtable `+0x50` | `0x006931C0`: formats each byte using `%02x` | `0x00695130`: writes the requested byte count |

Thus `10` means byte `0x10`, **not decimal ten**, and a hypothetical `a5` is a
byte value, not a malformed float. This establishes syntax and preservation;
it does not establish that arbitrary nonzero values are behaviorally safe.

No semantic bit test was identified in the inspected TDB load/save and core
editing paths. That is **not proof of global non-use**: accesses through copied
records, aliases or other subsystems would need further tracing. Do not label
the field “unused padding,” and do not invent flag names.

**TSRE-specific consequence:** Task 15's float parsing and literal-`00` writing
cannot preserve this field. A suitable future representation is `uint8_t
opaqueByte`, parsed as one hexadecimal byte and saved as two digits. A nonzero
round-trip fixture should accompany such a change. No TSRE implementation was
changed during this review.

### Endpoint metadata: fields 5/6 and `UiD[3]`

When the editor-mode global at `0x007BE0F8` is set, each vector section gets a
12-byte auxiliary allocation referenced by record `+0x04`:

| Auxiliary offset | Observed contents |
| --- | --- |
| `+0x00` | Initially zero; not a serialized field identified here |
| `+0x04` | Vector field 5, unsigned 32-bit |
| `+0x08` | Vector field 6, unsigned 32-bit |

The writer dereferences this allocation and writes both values. The driving
loader can omit that allocation; the values are therefore not required just
to follow/render the loaded vector geometry.

For end and junction nodes, the same editor-mode condition allocates an
8-byte metadata object referenced by node `+0x28`. The `UiD` reader puts field 3
in its first DWORD. The writer reads it back from there.

This **confirms native editor retention**, strengthening the case against
discarding these fields merely because ORTS ignores them. Their interpretation
as TrackShape endpoint indices is supported by TSRE's construction/reversal
logic and the user's implementation knowledge. This pass did **not** complete
the native placement-to-world-object trace needed to independently specify
endpoint numbering for every crossover, multi-path shape and internal boundary.
Keep that distinction explicit: storage is confirmed; the full native endpoint
numbering rule is still a follow-up.

`UiD` fields 0–2 use the same ownership helper (`0x005CA060`) as vector fields
2–4. `UiD` field 3 is unsigned 32-bit; fields 4/5 are signed-32 geometry tiles;
fields 6–11 are six float32 geometry values. World ownership must remain distinct
from endpoint geometry and from TDB node numbering.

## Junctions, end nodes and pins

| Field | Confirmed native behavior | What is not established |
| --- | --- | --- |
| `TrJunctionNode` first value | Reads unsigned 32-bit, retains low 16 bits at node `+0x4C`, writes it back zero-extended. | No semantic name established; not the enclosing node ID. |
| `TrJunctionNode` second value | Unsigned 32-bit TrackShape ID at `+0x54`; used to inspect the shape's route information during load. | Do not confuse this 32-bit field with the narrowed vector-record shape ID. |
| `TrJunctionNode` third value | Optional on input; low 16 bits retained at `+0x4E`. A missing value leaves the zero-initialized field; writer emits it. Also serialized by the separate state writer at `0x005CF5F2`. | Meaning unresolved. It is **not demonstrated to be selected route**. |
| Runtime junction route choice | Separate DWORD at `+0x50`; initialized by matching connected section information against the TrackShape route definition. | This separate field does not give the third file value a name. |
| `TrEndNode` value | Unsigned 32-bit at `+0x4C`, preserved. Value **3** gets special handling: the loader adds these nodes to a separate database-owned list. | Complete value enum and behavior of that list not resolved. “Always unused” would be wrong. |
| `TrEndLinkFile` child | Reader/writer retain a filename and a 32-bit hexadecimal value; reader's string buffer limit is `0x100` UTF-16 code units. Destructor frees the filename. | No external-file opening or destination-node resolution found in the traced paths. An unfinished cross-database linking design is plausible, not a demonstrated usable feature. See the focused trace below. |
| `TrPin` link | Unsigned numeric node reference in the file; converted to a node pointer after loading. Zero is treated as no link. | No spatial meaning demonstrated for the numeric ID. |
| `TrPin` direction | Numeric input retained as one byte in an 8-byte native pin record; writer zero-extends it. | Arbitrary byte values are not thereby valid directions. Keep established connection semantics. |

Junction reader/writer: `0x005CBD6B` / `0x005CBDF5`.
End reader/writer: `0x005CBE96` / `0x005CBFB4`.
Special end-list insertion: `0x005CDC91…0x005CDCBB`.

**TSRE-specific consequence:** the end-node value should not be discarded simply
because typical files contain zero. Preserve unknown junction values too.
Representing them as exact integers does not require assigning unproved names.

## TrEndLinkFile lifecycle and apparent external linking

Follow-up requested because no real route using this child was known to the
user. **The trace finds working persistence, not a working external connector.**

The native text structure is:

```text
TrEndNode (
    <unsigned end-node value>
    TrEndLinkFile ( "<filename>" <eight hexadecimal digits> )
)
```

This is a schema illustration, not a tested route recipe. The second child
value is an opaque 32-bit hexadecimal value; it has **not** been proved to be
a target node ID, pointer, flags word or world UiD. A decimal-looking `00000010`
means `0x10`. This is a different I/O primitive from the one-byte vector `00`.

| Stage | Observed use | Native address |
| --- | --- | --- |
| Node creation | Zero-initializes the end-node allocation, including the link fields. | `0x005CD2EE` |
| Parse | Reads end-node value into `+0x4C`. For `TrEndLinkFile`, reads the filename into a temporary UTF-16 buffer and the hex DWORD directly into `+0x54`. Duplicates a nonempty filename into `+0x50`. No file open or node lookup occurs here. | `0x005CBE96`; child body `0x005CBF14…0x005CBF89` |
| Register special ends | If node kind is end and its value is `3`, appends the node pointer to the database-owned list at database `+0x1C`. It does not inspect the filename or hex value to do this. | `0x005CDC91…0x005CDCBB` |
| Refresh | Adds missing value-3 ends to that same list, checking pointer membership first. | `0x005CCD97` |
| Save | Writes the end value; if `+0x50` is non-null, writes the child with that filename and hex DWORD. It does not require end value `3`, validate the target or translate the hex value through the output-node-ID map. | `0x005CBFB4` |
| Destroy node | Frees the filename allocation and clears its pointer. | `0x005CBE65`, dispatched by `0x005CD9AF` |
| Create/destroy database | Allocates and later destroys the separate value-3 list. | `0x005CEFE6` / `0x005CEE7D` |

Two important distinctions follow:

- **The child and value `3` are not parser-enforced prerequisites for one
  another.** An end value of zero can carry this child; value three can occur
  without it. Registration alone is not external connection resolution.
- **The hexadecimal field is not renumbered by the end writer.** That makes
  naming it a local TDB node ID premature. An external-database ID would not
  necessarily be subject to local renumbering either, so this does not settle
  its original intended meaning.

The focused core export covers functions starting in `0x005CB000…0x005CF100`:
load, graph editing, save and database lifetime. No consumer opening the stored
filename, locating its target, walking the value-3 list to resolve links, or
replacing a local pin with an external connection was identified there.
The generic database loader's visible callers and a broader field-offset
candidate search were also examined; they did not establish such a consumer.
Offset matches alone are not typed data-flow proof, because junction nodes and
many unrelated objects reuse offsets `+0x50/+0x54` for other purposes.

**Interpretation:** the name, stored filename/value pair and special-end list
are consistent with scaffolding for cross-file/cross-database track links.
That remains an inference. There is no evidence here for usable route merging,
streamed/paged TDBs or automatic loading of neighboring databases through this
child. A missing or unreachable resolver is a plausible explanation for why
normal routes never exposed this feature, but global non-use is not proved.

None of the six local census files contains `TrEndLinkFile`. This is not a
whole-stock-route census. No synthetic file was loaded in MSRE, so preservation
is established statically from the reader/writer, not a live round-trip test.

For TSRE, preserve it as an optional `{filename, opaqueHexValue}` if lossless
end-node support is implemented; do not start resolving files or synthesizing
cross-database connections based on this evidence. The reviewed TSRE writers
emit only the scalar `TrEndNode` value. The inspected Open Rails text reader
sets its end-node boolean and skips the block, so it supplies no additional
semantics for this child.

Evidence: `analysis/pe/ghidra-tdb-link-lifecycle.txt`,
`analysis/pe/ghidra-tdb-link-candidates.txt`,
`analysis/pe/ghidra-tdb-load-thunk-callers.txt`, and the audited disassembly.
`scripts/ghidra/ScanTdbLinkCandidates.java` is a reproducible candidate search,
not an exhaustive pointer-alias analysis.

## Does MSRE require nearby node IDs or paged TDB storage?

### Actual container and deletion behavior

`0x00578CDC` initializes an ordinary contiguous pointer array. Its header keeps
capacity, live count, highest occupied slot, growth increment and a next-free
candidate. The TDB loader passes `0x400` as the growth increment.

`0x00578D98` inserts into a free slot, or grows the contiguous allocation by that
increment. **The number 1,024 here is allocation growth, not a geographic page
size or a maximum distance between linked node IDs.**

`0x00578F59` removes an entry by clearing its slot, decrementing the live count,
adjusting the free-slot candidate and trimming an empty tail. It does not shift
every subsequent pointer. New insertion can reuse an earlier free slot, so even
native editing does not maintain a strict geographical node-ID ordering.

### Why saved IDs nevertheless look “shifted down”

At the start of the database writer, `0x005CC1EA` walks occupied slots and assigns
consecutive temporary output IDs to node `+0x04`:

```text
nextId = 1
for slot in existing slot order:
    if slot contains a node:
        node.outputId = nextId++

for slot in existing slot order:
    if slot contains a node:
        write TrackNode(node.outputId)
        for pin in node.pins:
            write TrPin(pin.target ? pin.target.outputId : 0, direction)
```

The pin target ID lookup is visible at `0x005CECEB…0x005CECFF`. After writing,
the database writer clears the temporary IDs. Node objects and pointer links do
not have to move. The numbering pass is linear in the slot span, and writing
each link performs a pointer lookup; this is not an expensive sequence of
whole-graph integer renumberings for each hole.

This preserves **relative slot order among surviving nodes**, unlike TSRE's
last-node-to-hole policy, but geographical proximity is not used by this code.

### A separate, real file-ordering constraint

There is a distinction between **which graph node gets ID 1** and **where the
`TrackNode(1)` block appears in the file**.

The native loader checks the declared ID for zero, capacity and occupied-slot
conflicts (`0x005CDA79`), but creation uses the ordinary first-free insertion
helper (`0x005CD2EE` → `0x00578D98`), rather than inserting directly at the
declared ID. The post-load link fixup indexes the pointer array by `link - 1`.

Consequently, write dense IDs **and their blocks in ascending ID order** for
MSTS interoperability. This is an instruction-backed loader constraint, not a
live permutation test. Arbitrarily shuffling blocks while keeping their old
declared IDs is not equivalent to consistently renumbering and writing a graph.
TSRE's current ascending-index serializer already follows the normal order;
this discovery alone does **not** explain its reported save-only failure.

## Save performance

The validation pass at `0x005B879C` searches the **whole item table for every
vector item reference**, comparing pointers. Instructions at
`0x005B8970…0x005B89A9` confirm that it does not stop after a match. For `R`
references and `I` table slots, that search costs approximately `R × I`
comparisons, in addition to the node traversal. About 30,000 references and
30,000 slots mean 900 million comparisons per pass. Items are not track nodes.

This is a concrete scaling problem and a plausible contributor to long saves,
not a measured attribution of every minute spent saving a large route. The
node-ID assignment itself is linear. Changing node order does not change this
validation loop's comparison count, so it does not explain the save crash.

## The save crash: what to investigate next

User-reported historical behavior: last-node-to-hole compaction worked on small
test routes, but moving nodes thousands of positions in large databases made
MSRE saving fail almost consistently. No failing snapshot remains, and a new
reproducer is not currently planned. This makes an ordering-sensitive editor
defect credible, without identifying its mechanism. The plan below is retained
for a possible future investigation, not a request to recreate the route now.

The current evidence does **not** justify blaming spatial paging or concluding
that swapping distant graph-node IDs is forbidden. Conversely, a readable TDB
is not necessarily editable: save touches metadata and consistency paths that
ordinary rendering does not exercise.

Relevant concrete differences and risks:

1. **Save uses editor-only metadata.** The vector writer dereferences the
   auxiliary object holding fields 5/6. This is an example of stricter save-time
   requirements, not evidence that TSRE compaction itself makes that pointer
   null. A normal MSRE load allocates it.
2. **Saving includes more than pins.** The coordinator at `0x005CE032` performs
   item-reference consistency work, writes the database and invokes the item
   sidecar writer. A crash reported as “save TDB” might be in those stages or
   elsewhere in route saving. `0x005B879C` checks vector item references and
   adjusts their track distances; it is also used during loading.
3. **TSRE remapping depends on graph invariants.** `TDB::deleteNulls()` visits
   the moved node's neighbors, calls `TRnode::podmienTrPin()` to replace a
   matching reverse link, and separately updates signal-direction junction
   IDs. It is not a global scan of every possible incoming reference.
   `podmienTrPin()` changes the first match per call. Valid reciprocal parallel
   connections may result in repeated calls; that alone is not proof of a bug.
   Pre-existing asymmetric/dangling links, unusual self-reference, stale
   counts or references outside the enumerated structures require tests.
4. **Editing defects are a confounder.** TSRE Task 15 already identifies
   lifetime/allocation issues. Their existence is not proof of this crash's
   cause, but a route changed through those operations is not a clean test of
   numbering alone.

The shortest useful follow-up needs a TDB that reliably loads but crashes on
MSRE save, its pre-TSRE counterpart if available, matching TIT/world files and
the required global/route track definitions. Preserve the failing snapshot.

Test on separate disposable copies, with no geometry or item edits:

| Variant | Purpose |
| --- | --- |
| Original known-good route, load and save | Establish a working baseline in the same executable/environment. |
| Stable-order dense renumbering | Match native output ordering with every node-ID reference updated. |
| Last-node-to-hole renumbering | Change only the numbering policy, not geometry, world ownership or item content. |
| Consistent swap of geographically distant nodes | Test locality independently of deletion/editing. Keep blocks in ascending new-ID order. |
| Swaps across IDs 1,024/1,025, plus same-size within-range control | Test the proposed allocation-boundary explanation without assuming it is real. |

Before a live run, check counts, dense ascending IDs, pin targets, reciprocal
links and directions, vector/item references, signal junction references and
exact preservation of all non-node-ID data. This field-census script does **not**
perform those graph checks. During a reproducing save, capture the faulting
instruction and stack: determine whether it fails in metadata, pin traversal,
item processing, serialization or world-object handling.

An isolated Linux Wine copy is a possible later test environment; no live test
is claimed here. Windows host access still requires explicit approval.

## Local census and remaining field questions

The six supplied installed-app TDB/RDB files contain **106 nodes and 630 vector
sections** in total. Three databases are empty. All 630 field-7 values are
`00`; all four sampled junctions have first/third values zero; all 64 sampled
end nodes have value zero. The nonempty files are Tutorial Route TDB/RDB and
the procedural test TDB. All scanned node IDs are dense and ascending.

This small corpus cannot establish absence of nonzero values in stock MSTS
generally or in third-party routes. It supplies no positive nonzero behavior
test. Counts and endpoint-value histograms are in the audit JSON.

Still unresolved:

- Semantic consumers and bit meanings of vector field 7.
- Full native TrackShape endpoint numbering, especially internal boundaries,
  crossovers and multi-path shapes.
- Meanings of the first/third junction values and the complete end-node enum.
- Whether an external end-link resolver exists outside the traced paths;
  preservation and value-3 list maintenance are confirmed, actual linking is not.
- The reported crash's actual instruction and the smallest numbering-only
  reproducer, if numbering alone is sufficient.

These are follow-ups, not reasons to discard the fields. The immediate safe
schema conclusion is to distinguish exact integers, geometry floats and the
single hexadecimal byte, and preserve unresolved values.
