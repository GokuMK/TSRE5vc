# MaSzyna rolling stock: what the files hold

A review for the [MaSzyna task set](../00-task-roadmap.md), 2026-11-09. It
records how the formats work and what the release pack really contains;
the design is in tasks [01](../01-e3d-models.md) and
[02](../02-vehicles-as-eng.md).

- Pack: `~/ORTS/maszyna`, 66 GB of `dynamic/`, 6 GB of `textures/`.
- Source: `../maszyna` at 66a940d4 (2026-10-21). MPL 2.0, read to learn the
  formats; no code is to be taken from it.
- Numbers were counted from the pack with throwaway scripts. "pkp" means
  `dynamic/pkp` (5,698 `.e3d`, 17.6 GB); "pack" means all of `dynamic/`
  (229 vehicle folders).

## A vehicle on disk

One folder holds a family of vehicles, for example `dynamic/pkp/et22_v3/`
(1.9 GB):

| File | Role | ET22 |
|---|---|---|
| `<type>.fiz` | Physics: mass, size, wheels, brakes, couplers, power | 20 |
| `<type>.mmd` | The rest: model, animation names, sounds, cabs | 21 (`cab0.mmd` is only an include) |
| `<skin>.mat`, `<skin>,1.mat`... | A livery: a texture set | 87 files |
| `textures.txt` | Catalogue of types and liveries for the launcher | 1 |
| `*.inc` | Text shared between `.fiz` and `.mmd` files | 12 |
| `201e/`, `201eo/`, `components/` | Models and their textures | |
| `skins/`, `normals/` | Livery textures | |

**What identifies a vehicle:** folder, type and skin. A scenery's `trainset`
block names all three per car (`simulation/simulationstateserializer.cpp:1518`).
There are no consist files in MaSzyna.

Across the pack: 1,453 `.fiz`+`.mmd` pairs (13 `.fiz` and 8 `.mmd` alone,
those are includes or leftovers); 937 pairs use `include`; 3,504 liveries
(after merging the `,1`...`,4` slot files), of which 3,361 (96 %) are listed
in a `textures.txt`.

### The catalogue, `textures.txt`
Read by `launcher/textures_scanner.cpp:28-190`. cp1250 text, one entry a line:

```
!=e,ET22,ET22-20xx,201Eo          category: first letter is the kind
*201E=?                           couplings
@201E=1                           drivable
201E-003.mat=201E,ET22,ET22-003//v2,ET22-003,PKPC,...   livery=type,icons//metadata
```

The kind letters: `e` electric, `s` diesel, `p` steam, `a` railcar, `z` EMU,
`r` utility, `d` draisine, `t` tram, `c` truck, `b` bus, `o` car, `h` person,
`f` animal; an upper-case letter is a carriage. After `//`: version, name,
short id, depot, overhaul date, overhaul company, texture author, photo
author.

## The model formats

There are two. **The pack holds no `.t3d` at all** (0 files, in `dynamic/`,
`models/` and `scenery/`), although every `.mmd` still names `.t3d`: the
loader drops the extension and takes `.e3d` first, then `.t3d`
(`model/MdlMngr.cpp:140-153`, `Model3d.cpp:1708-1726`).

### `.e3d`, binary
Reader `model/Model3d.cpp:2108-2491`, writer `:1825-1945`. Little-endian
chunks, each a four-character id and a `uint32` size including its 8-byte
header, inside one `E3D0` chunk:

| Chunk | Contents | In pkp |
|---|---|---|
| `SUB0` (`SUB1`...) | Submodels, fixed records of 256 bytes (`SUBn`: + 64·n) | `SUB0` in all |
| `TRA0` / `TRA1` | Matrices, 16 floats / doubles, column-major | `TRA0` in all |
| `VNT0` | Vertices 32 bytes: position, normal, uv | 14 files |
| `VNT1` | 16 bytes packed (half floats, 10-bit normal) | none |
| `VNT2` | 48 bytes: position, normal, uv, tangent (xyz + handedness) | 5,684 files |
| `VNTn` with bit 4 | Adds user data per vertex | none |
| `IDX1/2/4` | Indices, 1, 2 or 4 bytes each | 380 / 4,461 / 843 |
| `TEX0` | Material names, NUL-terminated, cp1250; entry 0 is empty | all |
| `NAM0` | Submodel names, same layout | all |

**Everything is already laid out for one draw buffer:** a submodel's record
gives offsets into the single vertex and index chunks, and its indices are
relative to its own first vertex. That layout is what MaSzyna then takes
apart again.

The submodel record (`Model3d.cpp:1757-1823`), offsets in bytes:

| At | Field | At | Field |
|---|---|---|---|
| 0 | next sibling (index, ≤0 none) | 48 | ambient (rgba) |
| 4 | first child (index, ≤0 none) | 64 | diffuse |
| 8 | type | 80 | specular |
| 12 | name (index or -1) | 96 | emission |
| 16 | built-in animation | 112 | wire size (unused) |
| 20 | flags | 116 | max distance² |
| 24 | matrix (index or -1) | 120 | min distance² |
| 28 | vertex count | 124-155 | light: near start/end, decay, radius, cosines |
| 32 | vertex offset | 156 | index count |
| 36 | material (index; 0 colour; -1..-4 skin slot) | 160 | index offset |
| 44 | light threshold | 164 | diffuse multiplier |

Types: 4 triangles, 256 pivot without geometry ("rotator"), 257 spot light,
258 stars, 259 text. Point sprites and text exist in the format; pkp has
243,857 meshes, 11,262 pivots, 5,643 spot lights and nothing else.

Flags that matter: `0x10` drawn in the opaque pass; `0x20` drawn in the
blended pass; `0x01`-`0x08` blended with skin slot 1-4; `0x4000` animated;
`0x8000` has a real matrix. **A matrix counts only when `0xC000` is set**
(`rendering/opengl33renderer.cpp:4043`). The top bytes summarise children and
siblings for skipping passes; TSRE does not need them.

Size range in pkp: vertices median 21,809, p90 147,296, max 1,050,022;
indices median 48,339, p90 411,915, max 1,449,195; submodels median 7, p90
167, max 744. ET22's `201e.e3d`: 6.7 MB, 108 submodels, 123,722 vertices,
357,102 indices.

### `.t3d`, text
Left out of TSRE (the user, 2026-11-09); recorded here for reference.
`Model3d.cpp:238-759` and `:2715-2764`. Whitespace tokens, `//` and `/* */`
comments, `include` with parameters. Each submodel repeats:

```
Parent: <name|none>  Type: mesh|freespotlight|...  Name: <name>  Anim: false|...
Ambient: ... Diffuse: ... Specular: ... SelfIllum: true|false|<n>
Wire: ... WireSize: ... Opacity: ... Map: <material>|none|-1...-4|replacableskin
MaxDistance: ... MinDistance: ... Transform: <16 numbers>
[userdata: ...] [numindices: n ...] numverts: n  <vertices>
```

Legacy vertices come per triangle: a smoothing mask (`-1` means explicit
normals), then position, optional normal and uv, Z-up. Normals are built
from the masks; indexed geometry brings normals and tangents. A parent must
come before its children. On load the whole model is turned to the scene's
axes (`InitialRotate`, `:817-892`), with geometry baked through matrices
where nothing moves.

### Space and hierarchy
Checked on the data, not only in the code:

- A `.e3d` is a tree: the root record (usually one, named `banan` or
  `harnas`) carries the turn `x' = -x, y' = z, z' = y`; below it the parts
  keep the modeller's Z-up space.
- The result is right-handed, **Y up, Z forward, X left**, origin at the
  centre of the vehicle on the rail head. ET22's bounds: X ±1.6, Y -0.03 to
  4.49, Z ±10.02. `+Z` is the end with cab 1.
- That is TSRE's drawing space (the same as glTF's; an MSTS shape gets there
  through its mirrored matrix 0). No mirror is needed.
- Front faces wind counter-clockwise: 45,312 triangles of ET22's body agree
  with their normals, 16 do not.
- A part's matrix is relative to its parent. An animation multiplies after
  it (`rest * animation`), so it turns about the part's own axes.
- 23,833 matrices (9 %) in pkp scale, and 385 mirror. Mirrored ones need
  the winding flipped, as glTF nodes do in TSRE.

### Levels of detail
Not a list of levels. **Every submodel has its own distance range**
(`min ≤ d < max`, stored squared) and a part out of range also hides its
children (`opengl33renderer.cpp:4043`, children drawn inside the check). The
distance is from the camera to the model's origin.

A vehicle's levels are made by hand in the model: ET22 has `pudlo` 0-100 m
and `lod_pudlo` 100-1000 m, `wheel01` 0-20 m and `Wheel07` 20-500 m under a
`bogie1` that itself ends at 200 m. Distinct limits per model: median 3, p90
7, max 21. ET22 uses 20, 40, 80, 100, 200, 300, 500, 1000.

Beyond the largest limit nothing is drawn.

Drawn pieces per model across the pack: at 5 m median 4, p90 151, max 727;
at 500 m median 2, p90 145, max 696. ET22: 87 at 5 m, 67 at 150 m, 57 at
500 m. The ranges cut triangles (ET22 116,000 to 25,000) far more than
draw calls.

### Materials
A name in `TEX0` or a `.mmd` is looked up as (`model/material.cpp:700-707`,
`model/Texture.cpp:1686-1704`):

1. Lower-cased. A `:traits` suffix is cut off (`tabor/cien:st`: clamp). A
   known extension is dropped.
2. In the vehicle's folder, then from the pack root, then in `textures/`,
   each trying `.mat`, then `.dds .tga .ktx .png .bmp .jpg .tex`.
3. No `.mat`: a material wrapping the texture.

`.mat` (`material.cpp:371-498`): `key: value` pairs, keys in lower case.

| Key | Count in pkp | Meaning |
|---|---|---|
| `texture1`, `texture2`... | 7,643 | Texture slots by number |
| `texture_diffuse` | 2,615 | Named slot |
| `opacity` | 1,072 | Alpha threshold |
| `texture_normalmap` | 649 | Named slot |
| `shader` | 345 | `parallax_specgloss` 174, `default` 96, `normalmap` 37, reflective 23... |
| `param_glossiness`, `param_specular` | 220, 207 | |
| `texture_specgloss`, `param_height_*` | 204, 174 | Parallax set |
| `winter:`, `rain:`... blocks | 6 files of 8,319 | Overrides by season and weather |
| `selfillum`, `size`, `shadow_rank` | few | |

A value may be a `[ a b c ]` set, one picked at random.

Without `shader:`, a second texture selects `default_2`, which is
`mat_reflmap.frag`: only that texture's **alpha** is used, as reflectivity.
ET22's liveries are that case (`texture2: normals/201e-037_normal`). Real
normal mapping needs `texture_normalmap:` or a `normalmap` shader.

`opacity` (`opengl33renderer.cpp:2333-2345`, `shaders/mat_default.frag:32`):
in the opaque pass a pixel with alpha below it is cut; in the blended pass a
pixel at or above it is cut. Unset: 0.5 and 0. A pair of passes therefore
splits a texture by alpha, which is how one livery holds bodywork and
windows.

The submodel's `Opacity` (`Model3d.cpp:398-455`) only picks the pass: below
1 blended, else opaque. `SelfIllum` is a light threshold: the part glows when
the scene is darker; `true` 2.0 always, `false` -1 never.

**Liveries** are skin slots (`vehicle/DynObj.cpp:240-294`). Parts with
material -1 to -4 draw with the vehicle's skin. A `#` after the model name
in `.mmd` (323 files directly) means several slots: `<skin>,1` to `<skin>,4` are
tried, else `<skin>` alone fills slot 1. A skin may also be `a|b|c`. In pkp
the slots are used 41,668 / 7,757 / 1,946 / 747 times.

Textures in pkp are all DDS DXT1 (3,942) or DXT5 (6,912), which TSRE reads.

### What a moved folder still needs
Resolving every reference in the pack the way above:

| Where the reference ends up | `.e3d` | `.mat` |
|---|---|---|
| The vehicle's folder | 13,022 | 9,005 |
| Its own folder, written from the pack root (`dynamic/pkp/et22_v3/...`) | 11,889 | 1,408 |
| `textures/` | 12,993 | 1,100 |
| Another vehicle's folder | 6,419 | 14 |
| Nowhere | 82 | 292 |

In `.e3d` names, 12,334 start with `dynamic/`, 4,692 with `textures/`, and 24
go up with `..`. `dynamic/` itself holds group folders (`bdz`, `cd`, `db`,
`gantry`, `parts`, `pkp`, `river`, `road`, `rzd`, `static`, `zssk`), which those
paths rely on.

So at least 198 of 229 folders need `textures/` and 60 need another
vehicle's folder.
`et22_v3` needs 24 files outside itself (19 MB): 23 in `textures/tabor` and
`textures/posers`, 1 in `303e_v1`, among them its coupler, ground shadow and
pantograph textures. All vehicles together need 698 files from `textures/`
(927 MB) and 780 from other vehicle folders.

### MaSzyna's main folders

What vehicles use of each, counted from the references in `dynamic/`:

| Folder | Size, files | Holds | Used by vehicles |
|---|---|---|---|
| `dynamic/` | 66 GB, 31,219 | The vehicles | Yes; 60 folders use another vehicle's |
| `textures/` | 6.0 GB, 16,298 | All textures | 698 files, 927 MB: `tabor` 403, `ladunki` 143, `posers` 106, `materials` 21, a few others |
| `sounds/` | 335 MB, 13,976 | All sounds | 3,994 files, 89 MB, by 226 of 229 folders; only 1 sound reference is inside a vehicle's folder |
| `data/` | 56 KB, 14 | `load_weights.txt`, `sound_overrides.txt`, `smokesource_*.txt` | Read by the simulator for all vehicles, not named by them (`application/application.cpp:1452`, `:1460`, `rendering/particles.cpp:453`) |
| `scripts/` | 12 MB, 285 | Python cab screens | Named by `pyscreen:` in `.mmd`; no Python in TSRE |
| `models/` | 1.4 GB, 3,358 | Scenery models | None: vehicles' load models are in their own folders |
| `scenery/`, `shaders/`, `fonts/`, `lang/`, `python64/`, `starter/`, `inne/`, `przepisy_kolejowe/`, `programy_na_potrzeby_symulatora/`, `screenshots/` | | The simulator, scenery, documents | No |

A sound's lookup is the vehicle's folder, the pack root, then `sounds/`
(`audio/audio.cpp:253-280`). `.mmd` files write `.wav` where the file is
`.ogg`, so these were counted by name without the extension; 4,897 of 145,145
references were found nowhere.

How vehicles write shared names (with `:traits` cut off, which the table in
the section above did not do):

- Textures: by a folder inside `textures/`, left to the lookup (`ladunki/atlas_loads`,
  7,230 in `.e3d`), or with the folder written (`textures/tabor/x`, 4,672 in
  `.e3d` and 1,100 in `.mat`; `/textures/tabor/x`, 1,670 in `.e3d`).
- Sounds: a bare name (`silence1.wav`, 139,841) or a folder inside `sounds/`
  (`sip/stacja.wav`, 406). Never with `sounds/` written.

Letter case: MaSzyna lower-cases what it opens, as a rule of the format. 176
of 32,182 paths in `dynamic/` have capitals (for example `202lc_v1/Ibhs_...`).

## `.mmd`

Read by `TDynamicObject::LoadMMediaFile`, `vehicle/DynObj.cpp:5181` to about
7,000. Tokens split by whitespace and `;`, in lower case unless asked
otherwise; `//` and `/* */` comments; `include <file> <params...> end`
(the text gets its `(p1)`, `(p2)`... replaced); `[ a b c ]` a random pick.
The whole file is wrapped as an include with name, type and skin as
parameters. Sections:

| Section | Used for | Matters for task set 1 |
|---|---|---|
| `models:` ... `endmodels` | Model file, `#`, animation names, `lowpolyinterior:`, `attachments:`, `loads:` | Yes |
| `locations:` | `doors:`, `tractionmotors:`, `bogies:` offsets | Bogies |
| `sounds:` | Sound definitions | Next tasks |
| `internaldata:` | `cab<n>model:`, `driver<n>pos:`, controls bound to submodel names, gauges, `pyscreen:` | Names only |
| `destination:`, `controllers:` | Signs, AI scripts | No |

Models drawn for one vehicle: the body, `lowpolyinterior` (named directly
by 491 `.mmd`; the interior seen from outside), up to several `attachments`
(9 directly), a load model. All with the same skin and space.

### How parts move
MaSzyna has no keyframes for vehicles. It finds parts by name once, then
sets each part's rotation or shift from the mover's state every frame.
The names come from `.mmd`, as a prefix plus a number. The counts come from
`animations: 12 0 0 4 2 2 -1`, in the order wheels, doors, levers, buffers,
bogies, pantographs, pistons, door steps, mirrors, wipers
(`vehicle/DynObj.h:32-42`).

| What | `.mmd` key | Part name | Motion | Code |
|---|---|---|---|---|
| Wheel | `animwheelprefix:` | prefix + 1..n, any case | Turn about x by the wheel angle | `DynObj.cpp:553` |
| Door | `animdoorprefix:` | prefix + 1..n | By `DoorOpenMethod` in `.fiz` (default turn): shift along z, turn about x, fold (three parts), plug | `:561-642` |
| Door step | `animstepprefix:` | prefix + 1..n | Shift or turn | `:663-693` |
| Mirror | `animmirrorprefix:` | prefix + 1..n | Turn about y; side from its z | `:695-714` |
| Wiper | `animwiperprefix:` | prefix + n + `_p1/_p2/_p3` | Turn about y | `:716-733` |
| Pendulum | `animpendulumprefix:` | prefix + 1..4 | Swing | `:5729` |
| Bogie | fixed | `bogie1/2`, or `boogie01/02` | Turn to the track | `:1466-1481`, `:2504` |
| Buffer | fixed | `buffer_left01`, `buffer_right01`... | Shift along x by the coupler | `:1048`, `:2436` |
| Pantograph | `animpantrd1/rd2/rg1/rg2/slprefix:` | prefix + 1..n, five parts each | Arms turn about x; signs and ratios by pantograph type | `:644-659`, `:5431-5602` |
| Rods and pistons | `animpiston...`, `animconrod...` | | Read and ignored in the code today | `:5665-5727` |

The pantograph's lengths and start angles are measured from the model's
matrices (`:5447-5492`), with `pantfactors:` as a fallback.

Two-state pairs: `TButton::Init` (`vehicle/Button.cpp:28-38`) looks for
`<name>_on` and `<name>_off` and shows one. Parts ending `_on` (exact case)
start hidden on vehicles (`Model3d.cpp:2511-2518`), `Light_On*` everywhere.
Names used
this way: `headlamp11_on`, `endsignal12_on`, `coupler1_on`, `pneumatic1_on`,
`cpneumatic1r_on`, and cab controls. Name search is case-insensitive and goes
self, next sibling, then children; the first found wins (`:1152-1180`), and
names repeat (ET22 has two `rynny`).

Other names with a meaning: `cien`, a fake ground shadow; `smokesource_*`,
particle emitters; `winter_variant` and its kind, for seasons.

Lights are `freespotlight` submodels under the `_on` parts.

## `.fiz`

`McZapkie/Mover.cpp:9557-9900`. Line based: a section word, then `Key=value`
pairs, the first value of a key counts (`fizlines.try_emplace`).
`include <file> <params...> end` as above. Multi-line tables between
`RList:`, `MotorParamTable0:` and their `END-` lines.

For the editors and the AI only a few lines are needed:

```
Param.      Category=train M=120000 Mred=18000 Vmax=125 PWR=3120 Type=et22
Dimensions: L=19.240 H=4.055 W=3.005 Cx=0.3
Wheels:     D=1.250 Axle=Co'Co' Ad=3.05 Bd=10.30
BuffCoupl.  CType=Screw ...
Engine:     EngineType=ElectricSeriesMotor ...
Load:       MaxLoad=52 LoadQ=pieces LoadAccepted=...
Doors:      DoorOpenMethod=Shift DoorMaxShiftL=0.65 DoorMaxShiftR=0.65 ...
```

Directly in a `.fiz`: `Category=train` 493, `road` 48, `ship` 1, the rest
inside includes. `EngineType`: electric series 83, induction 91, diesel 46,
diesel-electric 40, `Dumb` 29, `DumbDE` 18, `WheelsDriven` 8. Wagons have none.

## Room for extra values

All four files can take TSRE's own data without disturbing MaSzyna:

| File | What MaSzyna does with something unknown | Code |
|---|---|---|
| `.mmd` | Passes over each token it does not know, in every section | `vehicle/DynObj.cpp:5209-5213`, `:5905`, `:6523`, `:6617`, `:6976`, `:6980` |
| `.fiz` | Ignores a line it does not know; any line holding `#` is dropped | `McZapkie/Mover.cpp:9622-9627` |
| `.mat` | Skips the key and its value, or a whole `{ }` block | `model/material.cpp:474-483` |
| `.e3d` | Seeks past a chunk it does not know | `model/Model3d.cpp:2454` |

In `.mmd` a value must not be one of MaSzyna's own keys, or it would be taken
for one; a `tsre_` prefix on the key avoids the rest.

## Fit with `ComplexShape`

| `ComplexShape` needs | `.e3d` has | Note |
|---|---|---|
| Bounds, size, box | Computed through the matrices | |
| Parts: names, parents, rest transforms | Every submodel | Names repeat; first wins |
| Pose (`setPartTransform`) | Rest × motion, as above | Matches `partRest` × turn |
| Key frames (`partAtFrame`, `partLastFrame`) | **None** | Motion is computed |
| Distance levels | **Per-part ranges** | Levels derived from the limits |
| Sub-objects on and off | `_on`/`_off`, by name | By name, per state |
| Textures, skins | Slots -1 to -4 + `.mat` | In the shape's state |
| Lights | Spot lights under parts | Colour and cone only, no intensity |
| Emitters | `smokesource_*` pivots | |
| Displays | No (`pyscreen:` in `.mmd`) | |
| Tangents for normal maps | In `VNT2` | |

Nothing is missing that blocks drawing, posing or the viewers. The two
real differences are the key frames and the levels; both are handled inside
the shape, and tasks 01 and 02 describe how.

## What not to copy from MaSzyna

- Each vehicle in a consist re-reads its `.fiz` and `.mmd` from scratch
  (`DynObj.cpp:1987`, `:5181`), with sounds and animations resolved again.
- Tokens are allocated one string at a time and lower-cased; `.fiz` keys are
  substring searches per line.
- `.e3d` is copied value by value into a separate vector for each submodel
  and handed to the renderer one submodel at a time
  (`Model3d.cpp:2190-2259`, `:2489`).
- `GetFromName` walks the tree and lower-cases two strings at every node
  (`:1152-1180`), and is called for every animation and button of every car.
- `.t3d` normals search every later triangle for each vertex
  (`SeekFaceNormal`, `:186`): quadratic in the part's size.
- Each texture lookup probes up to 24 paths on disk.
- Parts are drawn one by one, walking the tree per pass, each binding its
  material.
