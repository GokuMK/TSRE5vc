# MSTS Bin 1.9 custom-terrain patcher

Updated 2026-09-05: the mandatory terrain patch now includes the successfully
retested R64 v5 changes. Command-line options and the required clean input are
unchanged; all four output hashes have changed.

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

For the complete test build—including direct route launch and the corrected,
editor-guarded 1280x800 Route Editor window, render target, and mouse
mapping—open Command Prompt in the patcher's directory and run:

```bat
py patch_msts_bin_1_8_terrain.py "C:\MagiPacks\Microsoft Train Simulator\train.exe" "C:\MagiPacks\Microsoft Train Simulator\train.terrain-test.exe" --full
```

The expected output is:

```text
Size:       4,366,336 bytes
SHA-256:    97b4ceda684b447a69878dcdc13e37af8783e2e35103ab6ed48cba8f4a1cfaf0
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
| `--full` | both conveniences | 4,366,336 | `97b4ceda684b447a69878dcdc13e37af8783e2e35103ab6ed48cba8f4a1cfaf0` |

The `--full` output is byte-identical to `train-wine-r64-v5.exe`, the combined
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
problematic route.

This is bounded rendering evidence. Multi-tile fully dense R64 terrain,
whole-tile error bias zero, and R64 height edit/save/reload have not received a
complete test matrix. P32/N1024 and R64 support therefore remain experimental,
even though both are now included in the main patcher.

Custom `.t` files intended for MSRE editing must name
`terrain_sample_ybuffer` (146), `terrain_sample_ebuffer` (147), and
`terrain_sample_nbuffer` (148). Missing E/N payloads can be regenerated, but
the resource entries must exist. A route should also contain a sound-source
world object to avoid the unrelated known Windows 10 Route Editor 1–2 FPS bug.

Open Rails compatibility depends on the branch:
the inspected master uses fixed R16 geometry,
while unstable derives R from N/P. The terrain-normal sample spacing
and P32 water grid have separate limitations. See the workspace's
`reports/msts-orts-terrain-profile-compatibility.md` for pinned revisions and
the per-profile distinctions; this patch changes MSTS only.

## Local verification

The 2026-09-05 update passed 13 regression tests in WSL. These cover all four
CLI profiles and complete output hashes, exact agreement at all 40 historical
v5 instruction sites, unchanged optional code/list relocation, both renderer
initializers, standalone execution, and input/overwrite protections. Reversing
only the 18 new instruction changes reproduces every previous R32 profile hash.
The full output was also compared byte-for-byte with the live-tested v5 file.

From the development workspace, rerun with:

```sh
python scripts/test_msts_terrain_patcher.py
```

The tests use the private clean executable in `proprietary/msts_app/train-1.8.exe`
(or `MSTS_TEST_SOURCE` pointing to another Linux-local copy). They skip if the
private fixture is absent. No application was launched and no Windows host was
accessed during patcher integration. Python 3.9 syntax was checked; execution
of this update was tested with the workspace's Python 3.14.

## Direct route launch

When `--direct-route` or `--full` is used, launch a route by its folder
identifier with MSTS's native colon separator:

```bat
train.exe -editroute:terrainsize
```

An absent or unknown route keeps the ordinary chooser behavior.
