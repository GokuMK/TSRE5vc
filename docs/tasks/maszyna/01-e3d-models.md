# Task 01 - MaSzyna models as a shape

## Objective

Draw `.e3d` as another `ComplexShape`, beside MSTS shapes and glTF: opened
in the Shape Viewer, with its hierarchy, materials, liveries and moving
parts available to whatever draws them. Not converted (the decision of
2026-10-24). Reading is TSRE's own and made for speed; MaSzyna's code is only
where the format was learned.

Status: design, 2026-11-09, with the user's first answers in it. Nothing
built. The formats are described in the
[review](reports/rolling-stock-format-review.md).

`.t3d` is left out (the user, 2026-11-09): obsolete, very slow to load, and
none in the pack. An `.mmd` still names `.t3d`; the name is used without its
extension and the `.e3d` taken. A model that exists only as `.t3d` is
reported and not loaded.

## Design

### One model per file, shared

`E3dFile::read` (new, `src/tsre/maszyna/`, no Qt GUI or renderer) turns a file
into one immutable model:

```cpp
struct E3dModel {
    struct Part {
        int parent;                 // always lower than this part's index
        int firstChild, nextSibling;
        int name;                   // into names, -1 none
        Kind kind;                  // Mesh, Pivot, Spot, Other
        float rest[16];             // relative to the parent, column major
        float world[16];            // at rest, through the parents
        unsigned firstIndex, indexCount; int baseVertex;
        unsigned firstVertex, vertexCount;
        int material;               // into materialNames; 0 colour; -1..-4 slot
        unsigned char passes;       // opaque, blended, blended by slot
        float rangeMin, rangeMax;   // metres
        bool startsHidden;          // "_on", "Light_On"
        ...                         // colours, light threshold, spot data
    };
    std::vector<Part> parts;        // parents first
    std::vector<float> vertices;    // RenderItem::PBR layout
    QByteArray indices;             // uint16 or uint32
    QStringList names, materialNames;
    QHash<QString, int> byName;     // lower case, first in MaSzyna's search order
    float bound[6], radius;
    std::vector<float> levelLimits; // sorted distance limits, metres
};
```

A `weak_ptr` cache by path and modification time shares one model, and one
GPU mesh, between everything using it, as `SimisDocument` shares its trees.
Liveries multiply: forty wagons with forty liveries are forty shapes over one
model. `reload()` goes past the cache.

### Reading

The file is read once into memory, and nothing allocates per vertex or per
submodel beyond the tables above:

1. One pass over the chunk headers, checking every size against the file.
2. `SUB0`: fixed records read in place; the tree is checked (indices in range,
   no cycles) and reordered so parents come first, which makes every later
   matrix update one forward pass.
3. `VNT2` to the PBR layout in one loop over the whole chunk (12 floats to
   19: position, normal, uv, alpha 1, tangent, uv2 0, colour 1). `VNT0` has
   no tangents: they are computed only when a material needs them.
4. `IDX2` and `IDX4` are kept as they are; `IDX1` is widened to 16 bits.
5. `TRA0` and `TEX0`, `NAM0`: copied once; names decoded from cp1250.

**One vertex buffer and one index buffer for the whole model.** A part's
record gives a range of them (`firstIndex`, `indexCount`, `baseVertex`),
exactly as the file stores it, so a part costs no copying and the renderer
gets one upload.

The `RenderItem::PBR` layout takes 76 bytes a vertex against the file's 48.
That is accepted for now (ET22's model: 9.4 MB, shared by every livery),
since a smaller layout means work in both renderer backends. To revisit if
memory shows it.

Problems in a file (a size past the end, an index out of range) stop that
model with a message, never a crash: the corpus suite below runs every file
in the pack.

### Parts and pose

Each submodel with a name, geometry or children is a part. The shape
implements the part API that `SFileComplex` has:

- `partCount`, `partName`, `partParent`, `partRest`, `partMatrix`.
- `partLastFrame` is -1 and `animationFrames` 0: there are no key frames.
  `setPartFrame` does nothing, and `partAtFrame` gives the rest pose.
- `setPartTransform(state, part, local)`: the local matrix given replaces the
  rest for that state, as it does for the MSTS backends. A caller composes
  `rest × motion`, the order MaSzyna uses.
- A state holds only the parts away from rest. Its world matrices are one
  forward pass over `parts`, and only when something changed.
- `matrixByName` and name lookups use `byName`: built once, in MaSzyna's
  order (self, next sibling, then children), so repeated names resolve the
  same way.
- Visibility: `enableSubObjByName(state, name, on)`, per state, starting from
  `startsHidden`. A hidden part hides its children, as a part out of range
  does. `setEnabledSubObjs` is the MSTS mask and is not used.

### Levels

`levelLimits` is the sorted set of all ranges' ends. Level *n* covers one
interval between limits, so the levels are exactly the distances where
something changes (ET22 has eight), and a part is in a level when it and all
its ancestors are in range there.

- `distanceLevelCount`, `distanceLevelDistance`: the limits. `ShapeLod` adds
  the shape's radius when choosing, MaSzyna does not, so the radius is taken
  off the limit given.
- `setCurrentDistanceLevel` per state, as for other shapes. LOD bias and
  forced levels work unchanged.
- Each level's packets are built on first use and kept, as `GltfShape` does.
- Past the largest limit MaSzyna draws nothing. TSRE keeps drawing the last
  level on, like every other shape (`ShapeLod::Extension::Default`); the last
  level is the last non-empty one.

### Drawing

Per level, a packet for each mesh part and pass: the shared mesh, the part's
index range, its material, and its matrix (`msMatrix`). A part in both
passes makes two packets. Unposed states share their packets, as glTF's do,
so repeated objects can be instanced.

Face winding needs no change. `mirrored` is set where a part's world matrix
turns it inside out (385 matrices in pkp), as for glTF nodes.

Draw calls: 87 packets for ET22 close up, and p90 151 across the pack. A
long train of such vehicles is thousands of packets. The fix would be to
merge parts that never move (same material and pass and range) at load, but
which parts never move is only known from the `.mmd`. It is left as the
last, optional step, done if a measurement says so.

### Materials and liveries

`.e3d` takes the route S files take to PBR (task shapes 13), as the user
expected. `MaszynaMaterial` (new, `src/tsre/maszyna/`) only reads `.mat` and
finds the textures; each file is parsed once. Season and weather blocks pick
by `Game::season`; a `[ a b c ]` set takes its first value, so a model looks
the same every load. A packet is then filled by the same
`MstsMaterials::fill`, as a `PbrTexDiff` or `BlendAPbrTexDiff` primitive, and
`MstsMaterials::tangents` serves the 14 files without tangents.

| MaSzyna | `MstsMaterials` |
|---|---|
| `texture1`, `texture_diffuse` | The packet's texture, base colour |
| `texture_normalmap`, or `texture2` with a `normalmap` shader | `normalTexture` |
| `texture2` with no shader (reflectivity in its alpha) | Ignored at first; a capture decides |
| `param_glossiness` | `Factors::roughness` |
| Nothing given | `Factors`' defaults: a dielectric of roughness 0.5 |
| Colour (no texture, material 0) | `Factors::tint` |
| Opaque pass | `Shader::Pbr`, alpha test if the texture has alpha |
| `.mat` `opacity` | `pbr.alphaCutoff`, set after `fill` |
| Blended pass | `Shader::PbrBlended`; `glass` off after `fill` for parts that are not glass (`cien`) |
| `SelfIllum` 2.0, or at or above 1 | `emissive`, its base texture as the emissive map |
| `SelfIllum` -1 | None |
| `texture_specgloss`, parallax, reflection maps | Not at first |
| Texture `:s`, `:t` traits | Clamp in that direction |

Every part goes this way, on the one vertex buffer. S files keep their plain
materials on `VNTA` with a second buffer for PBR parts; that split is not
needed here, since the file already brings tangents, and the plain route's
alpha test is fixed at 0.51 in the shader while a `.mat` gives its own (ET22's
liveries 0.92). The PBR programme already draws glTF shapes.

TSRE-only values need no `.sd`: a `.mat` takes extra keys that MaSzyna
ignores (`tsre_metalness: 0.2`, a `tsre_orm:` texture), if a livery ever wants
them.

Liveries are a new load option, `ShapeLoadOption::Skin`, like `DoubleSided`
is. Its value is a skin name or `a|b|c`. Slots -1 to -4 take `<skin>,1`..`,4`,
or `<skin>` for slot 1 if those are absent, as `material_data::assign` does.
A shape opened without a skin draws those parts in plain grey.

Lookups follow MaSzyna's order (vehicle's folder, pack root, `textures/`),
with its folders mapped by `MaszynaPaths`: a name in `dynamic/` is under
`[[Trainset]]`, shared textures under `[[Textures]]/MASZYNA`, with or without
`textures/` written in the name
([task 02](02-vehicles-as-eng.md#files-outside-the-vehicles-folder)). For a
bare file in the viewer, the vehicle's folder is the nearest parent holding
an `.mmd`, at most two levels up, else the file's own. A texture found nowhere
draws in the missing-texture colour, reported once.

Names are lower-cased before opening, since that is how the format is
defined; this is a spelling rule, not a search, so no folder is listed for
it. Files whose real names have capitals are reported as content problems.

### Threading

As `GltfShape`: `detachedCopy()`, `loadDetached()` on a worker reads the model
(or takes it from the cache) and the materials and lists the textures needed;
`adopt()` registers the textures on the main thread and builds the packets.
The mesh is created once for the shared model and released with its last
user on the main thread.

### `ComplexShape` in full

| Method | Here |
|---|---|
| `getPathId`, `getTexPath`, `isLoaded`, `hasLoadFailed`, `getSize`, `getBound`, `getBoxPoints` | As for glTF |
| `load`, `reload`, `setLoadOptions`, `detachedCopy`, `loadDetached`, `adopt` | Above |
| `newState`, `releaseState`, `setAnimated`, `updateSim` | State holds pose, visibility, level; no time-based animation |
| Distance level methods | Above |
| Part and pose methods | Above |
| `enableSubObjByName`, `enableSubObjByNameQueue` | Above |
| `pushRenderItem` (both) | Above |
| `invalidateRenderState` | Rebuild packets |
| `enablePart`, `disablePart` | By part, for the viewer |
| `drawnPrimitiveCount`, `drawnPrimitiveMaterial`, `pushPrimitiveHighlight` | A packet is a primitive |
| `fillShapeTextureInfo`, `fillShapeHierarchyInfo`, `fillContentHierarchyInfo`, `imageNames` | For the viewer windows |
| `getFloorBorderLinePoints`, `isSnapable`, `addSnapablePoints`, `displays`, `emitters`, `lights` | Default (none) in this task |
| `setTextureOverride` | Not needed; liveries are a load option |

`ShapeLib::addShape` picks the class by extension. `.e3d` goes to `E3dShape`,
and the Shape Viewer's two extension lists (`ShapeViewerWindow.cpp:599`,
`ShapeViewerNavigatorWidget.cpp:125`) take it.

## Not in this task

- Lights (the spot submodels), emitters, displays: the data is there; they
  belong with the tasks that switch them.
- Time-driven built-in animations (`seconds`, `wind`...): none on pkp's
  rolling stock.
- Text and star submodels: none in pkp.
- Models from `models/` and `scenery/` placed on a route.

## Steps

Each step ends with `build all`, `ctest` and the app suites.

1. **Reader.** `E3dFile` + suite `e3d-file` (small files built in the test:
   each chunk type, bad sizes, cycles) + `e3d-corpus --game-root <pack>`
   (every `.e3d` read: no failures, totals of the review reproduced, time in
   Release).
2. **Drawn.** `E3dShape` with parts' rest poses, levels, no materials
   (lit grey); viewer opens `.e3d`; capture of ET22 close up and at 300 m.
3. **Materials.** `MaszynaMaterial` through `MstsMaterials::fill`, `Skin`
   option, passes, alpha, normal maps; capture with a livery. The `:traits`,
   the V direction of textures and the `texture2` question are settled here
   on the captures.
4. **Parts.** Pose and visibility, name lookup, the viewer's hierarchy
   window; suite: a posed part moves its children, states do not disturb
   each other, a hidden part hides its children.
5. **Optional, if measured to matter:** merge parts that never move.

## Done when

The Shape Viewer opens any `.e3d` in the pack and the cab models, shows its
parts and textures, and ET22 looks right from close up to a kilometre,
with a livery. Loading times in Release in this document.
