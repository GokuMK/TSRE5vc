# MSTS Bin 1.9 terrain and Telepole wire patcher

Updated 2026-09-14: optional `--telepole-wires` restores procedural wires,
with seven segments per pole gap and a smooth parabolic sag curve.
`--full` now includes it along with direct route launch and the editor window fix.
The required clean input and the four profiles without wires are unchanged.

This package patches an existing, legally obtained `train.exe`; it does not
contain or distribute the Microsoft executable.

The mandatory patch raises the terrain limits to:

```text
terrain samples per side (N): up to 1024
patches per side (P):         up to 32
samples per patch (R=N/P):    up to 64
```

It also updates the Route Editor height-edit bitmap and seam handling for
N1024 and relocates both 4,096-entry visible-patch lists into a new writable PE
section with 16,384 entries each. The sample and patch counts must form a valid
terrain layout; these are ceilings, not an assertion that every combination
has been tested.

The R64 renderer changes retain the tested v5 capacities: 32,768 shared
vertices, 65,536 source/final uint16 indices, two 160,000-entry uint16 clipping
buffers, and the 160,000-unit auxiliary workspace parameter. The two clipping
buffers occupy 640,000 bytes together and are shared, not allocated per patch
or tile. These conservative sizes have not been reduced to an untested minimum.

The script is self-contained, uses only the Python standard library, and is
locked to this unmodified, non-widescreen MSTS Bin executable:

```text
Version:    MSTS Bin 1.8.052113
Size:       4,091,953 bytes
SHA-256:    69218fce876298c684a2140c7d3925a452c47bb10037ffd8c491f65c5c0c6e7a
```

It will reject every other executable, verify each complete instruction before
changing it, verify the complete generated file against its known SHA-256, and
write a separate output. It never replaces the input executable.

To upgrade from an earlier terrain patch, run this version against the **clean
Bin 1.8 executable again**, not against an already patched output. Repack
installations may already have a modified `train.exe`; check the hash above.

## Requirements

- Windows with Python 3.9 or later (`py` or `python`), or another system with
  Python 3.9 or later.
- The exact clean MSTS Bin 1.8.052113 `train.exe` identified above.

No assembler, compiler, binary patch utility, or Python package is needed.

## Recommended command

For the complete test build, including Telepole wires, direct route launch,
and the guarded 1280x800 editor window/render/mouse fix, run:

```bat
py patch_msts_bin_1_9-alpha.py "C:\MagiPacks\Microsoft Train Simulator\train.exe" "C:\MagiPacks\Microsoft Train Simulator\train.terrain-test.exe" --full
```

The expected output is:

```text
Size:       4,370,432 bytes
SHA-256:    3586692bb7bcb1c8e5672ee32244d139a3ddd77e254a6666478465cc84938ff7
```

The script only creates the file. Preserve the installed `train.exe`, then
manually copy or rename the generated executable as desired. Running or
installing it is also a separate manual action.

If the output argument is omitted, the patcher creates
`train.terrain-patched.exe` beside the input. Existing outputs are refused;
`--force` permits replacing the output file, never the source.

## Profiles

| Command options | Added conveniences | Size | Output SHA-256 |
|---|---|---:|---|
| none | terrain only | 4,358,144 | `69ad461e98fd2a2975907b2dbf65c3d1abf04a0a897127b420dc1ec6ad8d3c57` |
| `--direct-route` | `-editroute:ROUTE_FOLDER` | 4,362,240 | `380672ea44e299637b183b7bcddebcaaf14baa1f6d029c0aae04671184bb8571` |
| `--window-1280x800` | editor-guarded window/render/mouse size | 4,362,240 | `ca51d5cebbd1877aece42e1fc33fb0cd9cf1ed07193f8be7062221a7c1c88088` |
| `--direct-route --window-1280x800` | both conveniences, without wires | 4,366,336 | `97b4ceda684b447a69878dcdc13e37af8783e2e35103ab6ed48cba8f4a1cfaf0` |

The profile with both conveniences **without wires** is byte-identical to
`train-wine-r64-v5.exe`, the combined
P32/R64/direct-route/guarded-window build used in the successful Wine retest.
All four profiles differ from their previous R32 versions only at 18 complete
instruction sites; all 40 historical v5 terrain/editor instruction sites match
v5. The optional direct-route payload and guarded window code are unchanged.
The embedded direct-route payload still needs no GNU assembler/linker.

The first window implementation, output SHA-256
`a005a4550b8ceed04e5f66a2666df41d918ac15b82c2b6431ae729e5ac66dd16`,
passed MSRE testing but changed the shared graphics request in driving mode.
The user subsequently observed a non-opaque/misaligned cab overlay and a small
3D aperture. It must not be distributed as the general-purpose full build.

The current window profiles add two guarded request stubs in a separate
`.editwin` section. When MSTS's existing editor-mode flag is clear, they pass
the original 640x480 values. They request 1280x800 only in editor mode. This
window fix was subsequently user-confirmed working, including restored driving
cab-view behavior. This R64 update retains that fix byte-for-byte; it does not
introduce a new window patch or claim a new driving-mode runtime test.


Add `--telepole-wires` to any profile above:

| Options in addition to `--telepole-wires` | Size | Output SHA-256 |
|---|---:|---|
| none | 4,362,240 | `7520419d950d99e8ebbbc2220eec317bbec936ba9e448ea8b62ce51564dae0c8` |
| `--direct-route` | 4,366,336 | `2138023568d01accdceb88e36d733b9a8d74b26072c01a7fd7c4a1e3adf495ca` |
| `--window-1280x800` | 4,366,336 | `a2233a4761784cd9e8e1556538c2ac37f24add97c8c933417f76325aef8a42dc` |
| `--direct-route --window-1280x800` (equivalent to `--full`) | 4,370,432 | `3586692bb7bcb1c8e5672ee32244d139a3ddd77e254a6666478465cc84938ff7` |

## Procedural Telepole wires

A usable `telepole.dat`, its pole shapes, and `Telepole` world objects must exist.
The patch renders the
generated wire strips and fills their points on initial allocation;
MSTS's existing movement/rotation callbacks update them afterward. It adds a
Telepole pass in both editor and simulator, independent of route electrification.
Telepole wires are independent of the overhead-wire graphics preference.
There is no geometry allocation or regeneration per rendered frame.

Both loading and new placement use seven segments per gap. Interior heights
follow `linear_height - 4 × 0.5 m × t × (1 - t)`, with the pole attachments
unchanged. The original generator instead lowered every interior point by
0.5 m, which produced a flat middle even with more segments. The nominal sag
depth remains fixed; this is not a physical tension simulation.

For each supplied four-wire, three-pole span, storage increases from 28 to 60
XYZ points (336 to 720 bytes), and drawing from 24 to 56 line segments.

The implementation adds one 4 KiB executable section and changes seven complete
instruction sites. All preceding terrain, route-launch, and window changes
remain intact. The independently written `telepole_wires.S` is provided for
review; its verified bytes are embedded in the standalone Python script.

The supplied `procedural` route was compared in the isolated Wine/llvmpipe lab
with maximum detail and overhead wires enabled. The previous executable shows
bare poles; this build draws the connecting wires on both spans immediately.
New placement and endpoint movement were also checked with a third span.
See the workspace report
[`msts-telepole-wire-restoration.md`](../../reports/msts-telepole-wire-restoration.md)
for settings, exact evidence, and test limits. An editor exit crash on this
route reproduces in the previous build at the same native address; this patch
does not repair it. Windows rendering and driving-mode visual validation remain
separate practical checks.

## Terrain layouts and current test status

The one patched executable retains stock terrain support and admits the tested
custom layouts:

```text
N=512,  P=16, R=32
N=512,  P=32, R=16
N=1024, P=32, R=32
N=1024, P=16, R=64
```

Manual testing has confirmed rendering for both P32 layouts and a complete
3-by-3 `N=1024/P=32` tile grid. N512/P16/R32 height editing was also confirmed
after the terrain descriptors were corrected. The following P32/N1024 cases
have not yet received the complete runtime test matrix:

- the exact peak number of simultaneously submitted patch-list entries;
- a boundary-centred sixteen-tile visibility test;
- detailed MSRE height edit, save, and reload testing across several tiles.

For N1024/P16/R64, isolated Wine testing reproduced v1 corruption and verified
correct v5 dense-hover textured/wireframe rendering on the supplied `mini`
route. The user also retested v5 successfully on both `mini` and the older
problematic route. Earlier reports that v5 made no difference are superseded;
an earlier deployment mismatch is possible but unproven.

This is bounded rendering evidence. Multi-tile fully dense R64 terrain,
whole-tile error bias zero, and R64 height edit/save/reload have not received a
complete test matrix. P32/N1024 and R64 support therefore remain experimental,
even though both are now included in the main patcher.

Custom `.t` files intended for MSRE editing must name
`terrain_sample_ybuffer` (146), `terrain_sample_ebuffer` (147), and
`terrain_sample_nbuffer` (148). Missing E/N payloads can be regenerated, but
the resource entries must exist. A route should also contain a sound-source
world object to avoid the unrelated known Windows 10 Route Editor 1–2 FPS bug.

Current TSRE supports P32 selection by assigning selection colours to the 256
patches closest to the camera and mapping those temporary colours back to the
actual patches. Consequently, it can select patch records above 255, although
only the nearest 256 patches participate in a selection pass. Open Rails
compatibility depends on the branch: the inspected master uses fixed R16
geometry, while unstable derives R from N/P. The terrain-normal sample spacing
and P32 water grid have separate limitations. See the workspace's
`reports/msts-orts-terrain-profile-compatibility.md` for pinned revisions and
the per-profile distinctions; this patch changes MSTS only.

## Local verification

The 2026-09-14 update passed 17 regression tests. They cover all eight output
profiles, `--full`, exact hashes, preservation of the previous profiles outside
the seven hooks and added PE metadata/section, the historical terrain checks,
and the existing input/overwrite protections.

Additional tests assemble the published source and compare it to the embedded
payload, require no relocations, and execute the actual emitted x86 against
instrumented native-helper substitutes. They exercise strip boundaries, repeated
batch flushing, occupied batches, invalid/overflowing counts, unavailable
buffers, editor/simulator branches, independence from the overhead-wire
preference, empty collections, static/fast/slow update-list coverage, and the
initial-generation hook's stack/callback contract. Smoothing checks execute the
actual x87 hook, including known curve samples, positive/negative heights,
symmetry, endpoints, and preservation of the floating-point stack over 2,700
calls. Seven-segment strips are tested at every initial batch occupancy.

The smoothing follow-up and its live geometry/editing checks are recorded in
[`msts-telepole-wire-smoothing.md`](../../reports/msts-telepole-wire-smoothing.md).

From the development workspace:

```sh
python scripts/test_msts_terrain_patcher.py
```

The PE tests use `proprietary/msts_app/train-1.8.exe` (or `MSTS_TEST_SOURCE`).
The assembly/native tests require Linux x86 with GNU Binutils and GCC and use
only independently authored code, without an MSTS fixture. These tools are
**development test dependencies**, not requirements for patcher users.
Live MSTS tests run separately in the private offline Wine lab. No Windows
host was accessed.

## Direct route launch

When `--direct-route` or `--full` is used, launch a route by its folder
identifier with MSTS's native colon separator:

```bat
train.exe -editroute:terrainsize
```

An absent or unknown route keeps the ordinary chooser behavior.
