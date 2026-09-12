# MSTS world-file Telepole object

`Telepole` describes a run of telephone poles and wires between two endpoints.
Its full binary token ID is **`0x00040009` (262153)**: namespace 4, local ID 9.
The world form holds numeric placement/configuration data; resource filenames
are supplied separately by the route's `telepole.dat`.

## Structure

`Telepole ( ... )` is a child of `Tr_Worldfile`. The original FFEDIT grammar
allows the following child blocks in any order. Each block contains the listed
values, without an additional count. Binary integers are unsigned 32-bit;
floats are IEEE-754 binary32, stored little-endian.

| Block | Values | Meaning / remaining uncertainty |
| --- | --- | --- |
| `UiD` | uint | World-object identifier. |
| `Population` | uint | Pole count; the stock examples contain 2 and 3. |
| `StartPosition` | float × 3 | Start endpoint, XYZ. |
| `EndPosition` | float × 3 | End endpoint, XYZ. |
| `StartType` | uint | Start endpoint type. Enum meanings remain unknown; both examples use 0. |
| `EndType` | uint | End endpoint type. Enum meanings remain unknown; both examples use 0. |
| `StartDirection` | float | Start endpoint direction. Both examples use 90; exact axis, units and sign convention need confirmation. |
| `EndDirection` | float | End endpoint direction. Same uncertainty as `StartDirection`. |
| `Config` | uint | Configuration selector, apparently indexing `TPoleConfigData`; exact lookup and invalid-index behavior remain unverified. Both examples use 0 with one configuration. |
| `Quality` | uint | Quality setting; enum/behavior unknown. Absent in both examples. |
| `Position` | float × 3 | World-object position. In both examples it is the midpoint between the endpoints. |
| `Direction` | float × 3 | Legacy rotation components (`Rx Ry Rz` in the grammar). Rotation order and units remain unverified; both examples use zeroes. This is not a quaternion. |
| `MaxVisDistance` | float | Maximum visibility distance by grammar name; default and runtime application unverified. Absent in both examples. |
| `VDbId` | uint | Visibility database identifier; both examples use `4294967295` (`0xFFFFFFFF`). |

The grammar permits repetition and does not establish mandatory fields or
runtime defaults. TSRE retains the last value of each recognized property and
preserves absence, including the distinction between a missing property and an
explicit zero. These are TSRE reader rules, not a claim about native defaults.

## Resource configuration

The inspected USA1 `telepole.dat` contains one `TPoleConfig` inside
`TPoleConfigData`. Its `FileName` is `telepole.s`, its `Shadow` is `teleshad.s`,
its `Separation` is 10, and it supplies four `Wire` XYZ attachment positions.
The configuration's leading value is 4, matching those four wire entries.
The two world spans are 10 and 20 units long, with populations 2 and 3. This
supports interpreting `Population` as the number of poles, including endpoints.
Wire interpolation, endpoint type behavior and configuration defaults still
need native behavior research before implementing rendering.

A filename-discovery tool cannot resolve Telepole resources solely by looking
for `FileName` inside the world object. It must also inspect the route's
configuration file. TSRE's new world handler preserves the selector; it does
not implement this indirect dependency traversal.

## TSRE support

`TelepoleObj` is registered in both binary and UTF-16 world-object factories.
It reads all 14 grammar fields and saves normalized UTF-16 text through the
existing world-file save path. Binary child lengths/labels use the existing
bounded reader. There is no new binary world writer.

Finite floats use nine significant digits and checked text conversion so
binary-to-text-to-text round trips retain their values. The surrounding world
parser still uses ParserX. Telepole numeric conversion is local because
ParserX's float accumulation loses precision and mishandles `e+...` exponents.
Unknown blocks follow the existing world parser's skip behavior and are not
retained for lossless rewriting.

Only the shared `WorldObj::position` receives TSRE's Z-coordinate conversion on
load, reversed on save. Private endpoints and direction fields remain in source
coordinates. Loading does not generate poles, wires or GL resources; this
implementation provides persistence, without rendering or new-object placement.

## Evidence and validation

- Original FFEDIT `forms.hdr` and `worldfile.bnf` establish the token and schema.
- Original disc 2 `USA1.CAB`, `Routes/Usa1/World/w-011055+014282.w`:
  compressed file SHA-256
  `c15bbc5e81effa6a2e1e7c762675f468532795974bde43fade1a74f0f9f68e5d`.
  The two Telepole blocks start at offsets **3144** and **3341** in the
  decompressed SIMIS file (including its 32-byte header), with UiDs 1796 and
  1797. The first exactly matches the reported unclassified-token failure.
- The same cabinet's `Routes/Usa1/telepole.dat` supplies the resource evidence.
- `token-world` tests cover all fields, labels, missing properties, unsigned
  limits, float round trips, coordinate conversion, cloning and each truncated
  binary property. The optional `TSRE_TEST_TELEPOLE_WORLD` environment variable
  adds a read-only native world parse and per-Telepole text round-trip check.

Original assets stay outside the repository. Example private-corpus invocation:

```sh
QT_QPA_PLATFORM=offscreen \
TSRE_TEST_TELEPOLE_WORLD=/path/to/w-011055+014282.w \
./build/TSRE5vc --test --test-suite token-world
```

Validated on 2026-09-13: the application and standalone token-test target built
successfully; `simis_tokens` passed, and `token-world` passed **63 checks, zero
failures**, including both native Telepole round trips. The source file's hash
remained unchanged. No rendering validation applies to this persistence-only
implementation.
