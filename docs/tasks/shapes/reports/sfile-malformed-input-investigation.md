# Malformed shape input: bracket location and label recovery

## Scope and evidence

Follow-up to the [three-mode comparison](sfile-three-mode-comparison.md).
Original Windows trainset files were read-only throughout. The isolated Open Rails
probe links the unchanged local `ShapeFile.cs`, `SBR.cs`, `STFReader.cs` and TokenID
sources, using the existing .NET 8 / MonoGame setup. Shape validation is enabled.
This is execution of the actual parser, not a full OR rendering session.

Artifacts: `/root/shape-compat-20260911/parser-research/`. They include the probe
project/source, original-file results/warnings, synthetic label cases, a repaired
Linux-only ET41 copy, and source/executable SHA-256 hashes. No production parser
mitigation has been implemented.

## Exact missing-parenthesis location

Original: `/mnt/c/trainsim/trains/trainset/PKP_ET41_203E-OR/PKP_ET41-045B_z.s`.
The missing close belongs **after original line 121633, before line 121634**:

```text
121632  anim_node PantographTop2 (
121633      controllers ( 0 )
        )                         <-- missing close for PantographTop2
121634  anim_node PantographMiddle2A (
121635      controllers ( 0 )
121636  )
```

Geometry and LOD nesting are balanced before `animations` starts at line 121602.
The missing close accidentally nests subsequent animation nodes under
`PantographTop2`. A generic stack scan ends with the outer Shape opening unmatched
because later closing parentheses are consumed one level too deep. That does not
mean appending a close at EOF is the correct repair. The reported offset 4677894
is where EOF exposes the imbalance, not where the omission occurred.

VS Code opening/displaying the text does not establish shape-format validity.
Indentation alone also obscures this omission.

### Open Rails result

The original file loads with warnings, including an unexpected `anim_node` while
expecting the preceding node to close and an end-of-file depth mismatch. OR returns
26 animation entries, but entry 10 (zero-based) has a null name where the source
intends `PantographMiddle2A`. Thus acceptance is not clean semantic recovery.

Inserting exactly that missing close into a private Linux copy restores
`PantographMiddle2A` at entry 10 and the OR parser completes with **no warnings**.
The original Windows file is unchanged. This confirms the omission site and argues
against a general "append a parenthesis and call it recovered" policy.

## Meaning of the string following matrix

The text block form is `matrix [label] ( twelve matrix values )`. The name before
`(` is the optional block label, not an additional numeric matrix field. The original
Microsoft `UTILS/FFEDIT/newshape.bnf` describes the matrix payload as twelve floats;
OR's `SBR.ReadSubBlock` reads the optional label and `matrix` assigns it to `Name`.
Binary blocks carry a separate length-prefixed label as well.

A single unquoted token is a label. A label containing spaces can be represented
as one quoted string, for example `matrix "drzwi 1_002" (...)`. This spelling was
verified with the OR parser. Two unquoted tokens do not form one label under OR's
normal tokenization.

### Actual Open Rails behavior

Tested original: `PKOR_B-111A/PKPIC_B10nou-70915_M.s`.
OR loads the complete file with 23 matrix entries. In particular:

| Source spelling | Matrix index (zero-based) | OR Name |
| --- | ---: | --- |
| `matrix drzwi 1_002 (...)` | 21 | `drzwi` |
| `matrix drzwi 1_004 (...)` | 22 | `drzwi` |

Entries 13 and 14 likewise retain the common name `drzwi`. They remain separate
matrix entries with their own numeric values and indices; OR does not merge them
into one matrix object. Hierarchy and vertex-state references are numeric indices.
Names can still matter to name-based behavior, so replacing a duplicate short label
with a distinct full label is not automatically runtime-equivalent.

Mechanism: `SBR.ReadSubBlock` takes the first token as the label, then asks
`STFReader.VerifyStartOfBlock` / `MustMatch("(")` for the opening bracket. `MustMatch`
allows one unexpected token with a warning and retries. Consequently `1_002` or
`1_004` is discarded, the following `(` is accepted, and the matrix payload is read
without shifting its numeric fields. Recovery of arbitrarily many extra tokens is
not established by this behavior. Similar malformed labels occur in primitive
states and animation nodes in this coach and produce corresponding OR warnings.

Controlled two-matrix probes confirm:

| Labels in input | Parsed names | Result |
| --- | --- | --- |
| `drzwi_1_002`, `drzwi_1_004` | two distinct underscore names | success, no warnings |
| `"drzwi 1_002"`, `"drzwi 1_004"` | two distinct full names including spaces | success, no warnings |
| `drzwi 1_002`, `drzwi 1_004` | `drzwi`, `drzwi` | success, warning for each suffix |

References: [Open Rails block reader](https://github.com/openrails/openrails/blob/master/Source/Orts.Parsers.Msts/SBR.cs),
[Open Rails text reader](https://github.com/openrails/openrails/blob/master/Source/Orts.Parsers.Msts/STFReader.cs).
Execution evidence uses the local sources recorded in `source-hashes.json`; upstream
line numbers or implementation versions may differ.

## MSTS evidence boundary

Actual `train.exe` behavior for these malformed files has **not** been verified.
A separate attempt with Microsoft's original `FFEditC_Unicode.exe` ran under the
private offline Wine sandbox, with no Windows mounts exposed. It failed even the
single-token and quoted synthetic controls before reaching the label comparison.
That experiment is inconclusive and is not evidence that MSTS rejects the malformed
labels. FFEdit is also a separate utility; its behavior must not be represented as
an observed result from the MSTS runtime.

## Implication for mitigation

There is positive OR evidence for a narrow compatibility recovery of the spaced-label
case: retain the first label token, warn about the unexpected extra token, accept the
following `(`, and preserve the number/order of matrix entries. Concatenating tokens
into a new full runtime name would not emulate the observed OR behavior. Complete
storage/save handling must separately preserve or explicitly repair malformed source
syntax without silently changing label semantics.

## Animation degradation after user review

The user clarified that damaged animations must not prevent valid static geometry
from rendering. Text parsing now tracks whether detected syntax damage is confined
to the shape's animation section. This includes EOF at the shape root after a
terminal animations block consumes the final closing delimiter. Damage detected
elsewhere remains fatal, as do failures of static geometry validation.

SFileComplex skips runtime animation extraction for this case and reports Recovered,
allowing static GL initialization in Complete and Compact. This is degradation,
not reconstruction of missing animation nodes. Complete retains the damaged source
and original bytes; saving remains blocked until the source is repaired. Compact
keeps its existing no-save/reload requirement. Binary damage recovery is unchanged.

## Multi-word label proposal (not implemented)

A label is one optional name between a block keyword and its opening parenthesis.
The unambiguous spelling for the intended full name is:

```text
matrix "drzwi 1_002" ( ... )
```

OR's first-token recovery loses the suffix. It preserves matrix count/indexing but
cannot be assumed to preserve intended name-dependent animation behavior.

Labels belong to the generic SIMIS block header, not to a whitelist of matrix or
anim_node block types. OR's text ReadSubBlock reads an optional label for every
block, and its binary ReadSubBlock reads a label-length field for every block.
The earlier recommendation to restrict support to known label-bearing types was
too narrow.

A generic compatibility rule can interpret unquoted words as ONE full label once
the parser knows it is reading a block header. Valid single-token and quoted
labels keep their meanings. This deliberately differs from OR for malformed
multi-word input; emit a diagnostic and normalize the full name as a quoted label
when exporting under the chosen recovery policy.

The separate problem is identifying where a block header starts. SIMIS bodies can
mix scalar values with child blocks. For example, an SD shape body starts with a
filename string before ESD_Detail_Level and other child blocks. A schema-free scan
could mistake that filename for a block keyword and the following child keyword
for its label. This ambiguity already exists with single-token labels; unlimited
lookahead cannot solve it. OR avoids this particular ambiguity by having format
readers explicitly request scalar reads or block reads.

Therefore generic multi-word label support does not require a per-block-type label
whitelist. It requires a reliable block-start decision from the enclosing grammar
or caller. Our document reader currently uses layout hints plus lookahead; merely
extending that lookahead to scan until any opening parenthesis would be unsafe.
Keep quoted text/escapes intact and stop at a closing delimiter or EOF rather than
absorbing another block after a missing opening delimiter. Unknown mixed-content
blocks remain ambiguous without additional grammar information.

No label parsing or hardcoded animation matching changes accompany this discussion.

## Validation of animation degradation

- Full application build passed.
- Shape document suite: 224 checks, zero failures.
- Application CPU/GL suite: 104 checks, zero failures, including both retention
  modes, refusal to save damaged source, and geometry damage remaining Broken.
- Original ET41: Complete and Compact both report Recovered, initialize GL,
  retain three LODs and load four textures.
- Against the private one-bracket-repaired copy, each mode has identical static
  bounds, size, metadata, hierarchy, matrix names, part metadata, uploaded buffer
  hashes, transforms, direct/gather image hashes and picking hashes in all three
  LODs. Animated output is intentionally not compared: the damaged section is
  discarded for rendering.
- Windows source was mounted read-only throughout. Original SHA-256 remains
  `5f4cd862a576b349ef9ae33927bdcaeb037cffa8b87ab16368b22b3a4d090da9`.

Private reproducible runner and results:
`/root/shape-compat-20260911/animation-degradation/verify.py`, `results.json`,
`suite.log`, and the four per-mode/per-input logs. The repaired input is overlaid
read-only at the original pathname inside a temporary mount namespace so texture
lookup is identical; no original file is replaced on disk. These are targeted
correctness checks, not fresh loading benchmarks or a full corpus rerun.

## Native MSTS parser follow-up

Static executable inspection now identifies the native text block reader at
`0x00691780`, UTF-16 lexer at `0x006919c0`, and expected-kind helper at `0x00691940`.
The reader resolves the block keyword through the known token table, then accepts
an opening bracket or one text label followed by a required opening bracket.
Capitalization does not distinguish keywords from labels. Label position is not
checked against the keyword table. Separate scalar-reading operations and the
calling format reader provide context.

The unquoted `drzwi 1_002` header fails the native bracket check after reading
`drzwi`; it does not perform OR's suffix-skipping recovery. This is a static
parser finding, not proof of what the full simulator does after that failure.
The examined reader/lexer/helper/string-reader/comparison functions match the
official Microsoft update executable byte-for-byte. Details and evidence:
[/root/msts/reports/msts-text-block-label-parsing.md](/root/msts/reports/msts-text-block-label-parsing.md).


Implementation status: context-aware multi-word label recovery is now implemented.
See [the fix, validation and parsing-time comparison](sfile-label-recovery.md).
