# MSTS world-file Telepole object

`Telepole` describes a native MSTS run of poles and sagging wires between two
endpoints. Its full binary token ID is **`0x00040009` (262153)**: namespace 4,
local ID 9. Pole resources and wire attachment points come from the route's
`telepole.dat`, while each world object selects a configuration by index.

## World structure

`Telepole ( ... )` is a child of `Tr_Worldfile`. The original FFEDIT grammar
allows these child blocks in any order. Binary integers are unsigned 32-bit;
floats are IEEE-754 binary32, stored little-endian.

| Block | Values | TSRE behavior |
| --- | --- | --- |
| `UiD` | uint | World-object identifier. |
| `Population` | uint | Number of poles, including both endpoints. Preserved for an untouched existing object. |
| `StartPosition` | float × 3 | First endpoint in MSTS source coordinates. |
| `EndPosition` | float × 3 | Last endpoint in MSTS source coordinates. |
| `StartType` | uint | Preserved. Endpoint-type semantics remain unknown. |
| `EndType` | uint | Preserved. Endpoint-type semantics remain unknown. |
| `StartDirection` | float | Preserved. Native direction semantics remain unverified. |
| `EndDirection` | float | Preserved. Native direction semantics remain unverified. |
| `Config` | uint | Zero-based `TPoleConfigData` entry. |
| `Quality` | uint | Preserved when present; runtime semantics remain unknown. |
| `Position` | float × 3 | Object midpoint. TSRE updates it after endpoint edits. |
| `Direction` | float × 3 | Preserved legacy rotation components. They are not a quaternion. |
| `MaxVisDistance` | float | Preserved when present; not yet applied to rendering. |
| `VDbId` | uint | Visibility-database identifier. New objects use `0xFFFFFFFF`. |

The parser retains the last occurrence of every recognized field and preserves
whether an optional field was absent. Unknown child blocks follow the existing
world-parser skip behavior and are not retained for lossless rewriting.

## `telepole.dat`

`TelepoleData` reads the route-level `TPoleConfigData` catalog. Each
`TPoleConfig` currently supports:

- its declared wire count;
- `Filename`, resolved in the route `SHAPES` directory;
- `Shadow`, retained in the catalog but not rendered separately;
- positive `Separation` in metres;
- any number of `Wire ( X Y Z )` attachment records.

The world object's `Config` value is the original catalog index. Invalid
entries remain addressable for diagnostics and existing data, but the placement
selector offers only entries with a usable filename and separation.

## Rendering

TSRE loads the configured MSTS pole shape through the shared shape library and
instances it at every derived pole point. X/Z positions are linearly
interpolated, and every pole—including both endpoints—is grounded using the
terrain-height query. Authored endpoint heights remain unchanged in the world
data and population calculation. Existing `Population` is authoritative until
the user changes an endpoint or configuration.

Wires use the existing ORTS-profile mesh backend with an in-code square
cross-section. Each span is sampled at no more than one metre, with at least
four subdivisions. The vertical sag is a simple parabola:

```text
y(t) = lerp(y0, y1, t) - 4 * (spanLength * 0.02) * t * (1 - t)
```

The wire frame is upright, so terrain slope does not roll the attachment
layout. Generated sample spans are combined into one draw object per
LOD/material instead of producing a draw call for every metre. Rendering is
limited to 10,000 derived poles as protection against corrupt data; the stored
world value is not truncated.

Current rendering limitations:

- `StartType`, `EndType`, the direction fields, `Quality`, `Shadow`, and
  `MaxVisDistance` are preserved but do not alter the generated result;
- the first `Wire` coordinate, interpreted as a longitudinal attachment
  offset, is retained by the catalog but not yet applied; stock examples use
  zero;
- wire radius, colour, sag ratio, and sampling distance are code defaults, not
  editable route settings.

## Placement and editing

Each valid configuration in the current route's `telepole.dat` is exposed as
an editor-generated REF item under **Other > Telepoles**. These transient items
carry the configuration index in `RefItem::value`; they neither extend nor get
written to the route's MSTS REF file. Select an item and use the ordinary
**PLACE** tool.

1. The first click places the first endpoint and starts a live preview.
2. Mouse movement updates the second endpoint. The mouse wheel applies the
   shared continuous-placement height offset; holding Ctrl uses the fine step.
3. The second click accepts the native Telepole object. Escape discards only
   the unfinished span; PLACE and the selected Telepole REF item remain active.

Generic automatic placement is disabled for these REF items because it cannot
define the required second endpoint.

Only the two endpoint handles are selectable. Moving an endpoint or changing
configuration recalculates population as:

```text
max(2, ceil(threeDimensionalEndpointDistance / Separation) + 1)
```

Moving the object body translates both endpoints without changing their
spacing. The properties panel shows UID, tile, 3D length, pole count,
separation, and configuration, and configuration changes participate in undo.
Normal object placement and endpoint editing use the existing world-object undo
flow.

Coordinates deliberately remain split into tile indices and tile-local floats.
Only the small tile delta is combined when converting an edited endpoint. This
avoids the precision loss which occurs when the complete MSTS world coordinate
is collapsed into one float.

## Persistence

`TelepoleObj` is registered in both binary and UTF-16 world-object factories
and saves normalized UTF-16 text through the existing world-file path. There is
no new binary world writer. Private endpoints and direction values remain in
MSTS source coordinates; conversion to TSRE's OpenGL Z convention happens only
at the rendering/editing boundary.

Finite floats use nine significant digits and checked text conversion so a
binary-to-text-to-text round trip retains binary32 values. Telepole parsing uses
local checked numeric conversion because the legacy `ParserX::GetNumber`
accumulator loses precision and mishandles positive exponent signs.

## Evidence and validation

- Original FFEDIT `forms.hdr` and `worldfile.bnf` establish the token and
  schema.
- Original disc 2 `USA1.CAB`,
  `Routes/Usa1/World/w-011055+014282.w`, contains two native Telepole blocks;
  its `Routes/Usa1/telepole.dat` supplies the configuration evidence.
- `token-world` covers every world field, labels, missing properties, unsigned
  limits, float round trips, coordinate conversion, cloning, truncated binary
  fields, route-catalog parsing, and placement population calculation.
- A smoke run loaded the real `procedural` route, its native Telepole world
  objects, `telepole.dat`, and `telepole.s` without parser or rendering errors.

The optional `TSRE_TEST_TELEPOLE_WORLD` environment variable adds a read-only
native world parse and per-object text round-trip check:

```sh
QT_QPA_PLATFORM=offscreen \
TSRE_TEST_TELEPOLE_WORLD=/path/to/w-011055+014282.w \
./build/TSRE5vc --test --test-suite token-world
```

Implementation validation was refreshed on 2026-09-28. Visual acceptance of
pole orientation, wire attachment alignment, and terrain grounding remains a
manual route-editor test rather than an automated claim.
