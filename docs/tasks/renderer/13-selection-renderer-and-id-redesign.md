# Task 13 - Selection Renderer And 32-bit ID Redesign

## Objective

Move route-editor picking to a dedicated unsigned-integer render target, then replace the nearly exhausted 24-bit RGB selection encoding with a 32-bit encoding.

The redesign must:

- increase the main selector from 4 to 5 bits;
- preserve the current 16-bit dense world-object index;
- allow 2,048 selectable parts per world object;
- allow 4,096 directly addressed terrain patches;
- give activity objects and activity service/consist entries 2,048 selectable parts;
- retain unused selector values and payload space for future features;
- work identically in the legacy and gather renderers.

The selection ID is a transient runtime value. A world-object index is the dense index in the tile's object array, not the persistent MSTS `UiD`.

## Current Encoding

The route editor currently writes a 24-bit selection value through normalized RGB color channels. Its common prefix is a 4-bit selector in bits 23-20.

| Selector | Current category | Current payload |
| ---: | --- | --- |
| 0 | No hit | All remaining bits zero |
| 1-9 | World object in one of the 3x3 loaded tiles | Object index 16 bits, part 4 bits |
| 10 | Terrain | X 2 bits, Z 2 bits, camera-window patch value 8 bits; remaining bits unused |
| 11 | Activity object | Activity ID 12 bits, element 8 bits |
| 12 | Track/road database item | Database kind 1 bit, item ID 16 bits; remaining bits unused |
| 13 | Activity service/consist | Consist/service ID 12 bits, element 8 bits |
| 14-15 | Unused | - |

The existing world-object base value is `objectIndex << 4`, so only bits 3-0 are available to code that adds a part with a raw bitwise OR. Extending the part field therefore requires repacking the entire selection value, not only changing its decoder.

## Stage 1 - Selection Renderer

### Goal

Introduce the new rendering and readback mechanism without changing the logical values produced by the current 24-bit codec. This separates render-target risk from the selection-ID migration.

### Render Target

Create a reusable, single-sample selection framebuffer with:

- one `GL_R32UI` color attachment;
- a depth attachment compatible with the normal scene depth requirements;
- dimensions equal to the physical viewport size, including the device pixel ratio;
- creation, resize, framebuffer-completeness checking, and release handled in one owner.

The framebuffer stores one `uint32_t` selection ID per pixel. Channel order, normalized color conversion, and alpha blending no longer participate in picking.

Clear and read the integer attachment with integer operations:

```cpp
const GLuint noSelection = 0;
glClearBufferuiv(GL_COLOR, 0, &noSelection);
glClear(GL_DEPTH_BUFFER_BIT);

GLuint selectionId = 0;
glReadPixels(
    mouseX,
    selectionHeight - mouseY - 1,
    1,
    1,
    GL_RED_INTEGER,
    GL_UNSIGNED_INT,
    &selectionId);
```

### Selection Shader

Use a dedicated selection fragment shader bound at the start of the selection pass:

```glsl
layout(location = 0) out uint selectionResult;
uniform uint selectionId;

void main() {
    selectionResult = selectionId;
}
```

Use an unsigned uniform and an unsigned fragment output. Do not transport the ID through `vec3`, `vec4`, normalized RGBA, or texture alpha.

Objects that need cutout selection may still sample their texture and discard transparent fragments before writing `selectionId`. Texture alpha determines coverage in that case; it is not the selection ID storage.

### Shared Pass Integration

Both `paintGL2` and the gather renderer must render into the same selection target. Refactor framebuffer selection out of the scene-rendering body so a pass can explicitly target:

1. the QOpenGLWidget's `defaultFramebufferObject()` for visible rendering;
2. the integer selection framebuffer for picking;
3. a no-write validation mode where still required.

The existing gather call with drawing disabled is a validation/dry-render path, not an offscreen render target. It must not be treated as the selection framebuffer implementation.

For the selection pass:

- enable color and depth writes;
- disable blending, dithering, multisampling, and framebuffer sRGB conversion where enabled;
- use exact integer writes and a single-sample target;
- set the viewport to the selection attachment size;
- restore every changed framebuffer and GL state after the pass.

Avoid binding framebuffer `0` inside shared scene-rendering code. The visible QOpenGLWidget target is obtained with `defaultFramebufferObject()`, which is not guaranteed to be zero.

### Stage 1 Compatibility

Initially convert the existing 24-bit selection number directly to `uint32_t` and write that value to `GL_R32UI`. Keep the current selector values and decoder unchanged during this stage.

This provides a clean parity checkpoint:

- every existing selection category produces the same logical number as before;
- legacy and gather renderers return the same hit;
- background reads as zero;
- selection does not flash or modify the visible framebuffer;
- high-DPI coordinates and framebuffer resize are correct;
- transparent/cutout geometry has the intended selectable coverage;
- normal rendering state is unchanged after a pick;
- selection readback works with alpha blending enabled for the normal scene because blending is independently disabled for the integer selection pass.

## Stage 2 - Selection Redesign

### Common 32-bit Envelope

All route-editor selection IDs use this common envelope:

```text
31                         27 26                            0
+----------------------------+-------------------------------+
|         selector:5         |          payload:27           |
+----------------------------+-------------------------------+
```

```cpp
constexpr uint32_t SelectionSelectorShift = 27;
constexpr uint32_t SelectionSelectorMask  = 0x1Fu;
constexpr uint32_t SelectionPayloadMask   = 0x07FFFFFFu;
```

Use `uint32_t`, `GLuint`, or `quint32` throughout. A signed `int` is unsuitable because valid future selector values can set bit 31.

Keep the current category numbers so logs and debugging remain familiar:

| Selector | Category |
| ---: | --- |
| 0 | No hit / background |
| 1-9 | World object and 3x3 tile position |
| 10 | Terrain |
| 11 | Activity object |
| 12 | Track/road database item |
| 13 | Activity service/consist |
| 14-31 | Reserved |

ID zero is always the no-hit sentinel. Reserved selectors must decode as unknown rather than being interpreted as another category.

### World Objects

```text
31             27 26                  11 10                 0
+----------------+----------------------+--------------------+
| tile selector:5|   object index:16    |      part:11       |
+----------------+----------------------+--------------------+
```

- Tile selector: `1..9`, preserving the current 3x3 tile mapping.
- Object index: `0..65,535`, preserving today's dense runtime object-index capacity.
- Part: `0..2,047`.

```cpp
id = (uint32_t(tileSelector) << 27)
   | (uint32_t(objectIndex) << 11)
   | uint32_t(part);
```

An object requiring more than 2,048 independently selectable parts must be split into smaller logical objects. Producers must reject or diagnose an out-of-range part instead of truncating it and causing a collision.

### Terrain

Put the direct patch ID immediately after the X/Z tile location. This follows the same ordering as world selection: selector, location, primary object, subpart/feature.

```text
31          27 26  25 24  23 22             11 10            0
+-------------+------+------+-----------------+---------------+
| selector:5  | X:2  | Z:2  |  patch ID:12    |  feature:11   |
+-------------+------+------+-----------------+---------------+
```

- Selector: `10`.
- X and Z: `0..2` for the relative `-1..+1` tile position; code `3` remains reserved on each axis.
- Patch ID: `0..4,095`, supporting a possible future 64x64 patch grid.
- Feature: `0..2,047`; zero represents the normal patch until additional selectable terrain features are defined.

```cpp
id = (10u << 27)
   | (uint32_t(tileX) << 25)
   | (uint32_t(tileZ) << 23)
   | (uint32_t(patchId) << 11)
   | uint32_t(feature);
```

RGB channel boundaries have no meaning in the integer target, so the fields should be ordered for the data model and decoder rather than for R/G/B presentation.

Once the new codec is active, remove the camera-relative 8-bit terrain selection window and encode the patch directly.

### Activity Objects

```text
31          27 26                  11 10                 0
+-------------+----------------------+--------------------+
| selector:5  | activity object:16   |      part:11       |
+-------------+----------------------+--------------------+
```

- Selector: `11`.
- Activity object ID: `0..65,535`.
- Part/element: `0..2,047`.

### Track And Road Database Items

```text
31          27 26       24 23                              0
+-------------+-----------+---------------------------------+
| selector:5  | DB kind:3 |            item ID:24           |
+-------------+-----------+---------------------------------+
```

- Selector: `12`.
- Database kind: `0` track, `1` road, `2..7` reserved.
- Item ID: `0..16,777,215`.

This category has no selectable part today, so its payload is retained for a larger item ID and future database kinds rather than forcing it into the object/part format.

### Activity Service And Consist

```text
31          27 26                  11 10                 0
+-------------+----------------------+--------------------+
| selector:5  | service/consist:16   |      part:11       |
+-------------+----------------------+--------------------+
```

- Selector: `13`.
- Service/consist ID: `0..65,535`.
- Part, vehicle, or element: `0..2,047`.

The current player-service case may continue to use primary ID zero; it is distinct from the no-hit sentinel because selector 13 is nonzero.

## Codec And API Requirements

Create one selection-ID codec used by every producer and the central decoder. Do not scatter shifts, masks, float conversions, or raw OR expressions through render code.

The codec should provide typed helpers equivalent to:

```cpp
uint32_t worldObject(TileSlot tile, uint16_t objectIndex, uint16_t part);
uint32_t terrain(TileOffset x, TileOffset z, uint16_t patchId, uint16_t feature);
uint32_t activityObject(uint16_t activityId, uint16_t part);
uint32_t databaseItem(DatabaseKind kind, uint32_t itemId);
uint32_t activityService(uint16_t serviceId, uint16_t part);
DecodedSelection decode(uint32_t id);
```

Migrate names such as `selectionColor` to `selectionId`. Replace the color-setting selection API with an explicit unsigned-ID API, including queued `RenderItem` data. A helper that changes a part must clear and replace the defined 11-bit field; it must not assume that arbitrary bits can safely be ORed into an existing base.

Validate every field before packing. Debug builds should assert invalid values, while release builds should report and skip an invalid selectable item rather than silently masking it into another valid ID.

## Migration Order

1. Complete Stage 1 and verify the unchanged 24-bit codec through `GL_R32UI` in both renderers.
2. Add the centralized 32-bit codec and unit tests without changing producers.
3. Migrate world-object producers and decoding.
4. Migrate terrain producers, use direct patch IDs, and remove the 8-bit camera selection window.
5. Migrate activity-object, track/road-item, and activity service/consist producers.
6. Remove obsolete RGB packing, normalized color uniforms, and legacy decoder masks.
7. Run selection parity tests in both renderers and re-run the final renderer parity gate.

## Tests And Acceptance Criteria

Codec tests must cover round trips and rejected overflow for:

- selectors `0`, `1`, `9`, `10`, `11`, `12`, `13`, and reserved values;
- world object indices `0`, `65,535` and parts `0`, `15`, `16`, `255`, `256`, `2,047`;
- every valid terrain X/Z code, reserved X/Z code `3`, patch IDs `0`, `255`, `256`, `1,023`, `1,024`, `4,095`, and features `0`, `2,047`;
- activity object and service/consist primary IDs at `0` and `65,535`, with parts at `0` and `2,047`;
- database kinds `0`, `1`, reserved kinds, and item IDs `0`, `65,535`, `65,536`, `16,777,215`;
- world objects whose persistent `UiD` values contain large gaps, proving selection uses the dense tile object index;
- the zero/no-hit sentinel and invalid or reserved decoder results.

Renderer integration tests must verify the same selected category and payload in both pipelines, including overlapping geometry, terrain patch boundaries, cutout objects, high-DPI coordinates, viewport resizing, and a normal visible frame immediately after selection.

Stage 2 is complete when no route-editor selection producer depends on RGBA byte layout, every category uses the central 32-bit codec, all specified ranges round-trip without collisions, and both renderer paths pass selection parity.

## Scope Boundary

Shape Viewer uses a separate selection protocol and decoder. It must continue to work, but migration of its IDs into this route-editor selector namespace is not required by this task. The reusable integer selection target may be adopted there in a separate follow-up.
