# MSTS custom terrain-grid compatibility

## Result

The supplied `test_data/-11ced50c.t_256-8` is structurally valid, but it
describes a terrain layout which the patched MSTS `train.exe` explicitly
rejects.

MSTS parses `terrain_nsamples` (140) and `terrain_patchset_npatches` (161)
dynamically.  It then computes the samples per patch side:

```text
R = terrain_nsamples / terrain_patchset_npatches
```

The terrain-registration path accepts a tile only when both of these limits
hold:

```text
terrain_nsamples <= 256
R <= 16
```

For that tile, `N=256`, `P=8`, and therefore `R=32`.  It fails the
second limit.  A `256 / 4` tile has `R=64` and fails the same limit.  A
`512 / 16` tile has `R=32` and fails both limits (`N > 256` and `R > 16`).

The later `test_data/-11ced50c.t_128-16` control contains `N=128`, a 16 m
sample spacing, `P=16`, and therefore `R=8`.  The user reports that it loads
successfully in MSRE.  This positive runtime observation confirms that MSTS
does not require exactly 256 samples or an 8 m spacing; the earlier result is
about upper bounds, not a fixed standard layout.  The Windows run was
performed by the user and was not reproduced from WSL.

This is an MSTS executable constraint, not a remaining hard-coded TSRE load
loop.  The dynamic-grid work in TSRE can support layouts which legacy MSTS
cannot.

## Scope and artifacts

This review used Linux/WSL files only.  No Windows filesystem or process was
accessed.

- Rejected custom descriptor: `test_data/-11ced50c.t_256-8`
  - size: 5,026 bytes
  - SHA-256:
    `ac5bdb65b461db5d09e89eae83e6bc3b57c9cf8ae90fa4e4a22246039241c78b`
- Passing custom descriptor reported by the user:
  `test_data/-11ced50c.t_128-16`
  - size: 18,610 bytes
  - SHA-256:
    `168a5ed13bc13bc4fa74d57ab284b42d49528274e254ea34d1af541828b0d6db`
- MSTS executable: `proprietary/extracted/official-update/train.exe`
  - SHA-256:
    `730b5054adc73c2cbfb0b3eb6eb2d9d95ae339fcc3922fe74cb318d747319584`
- Synced TSRE source: `/root/TSRE5vc`, commit `6d77046`

The referenced `-11ced50c_y.raw` was not included in `test_data` and was not
found elsewhere in the WSL workspace.  Its contents and length therefore
could not be audited.  The `N=128` descriptor needs exactly
`128*128*2 = 32,768` bytes, while the `N=256` descriptor needs
`256*256*2 = 131,072` bytes.  A missing or short RAW file is an independent
load failure, but it does not explain the layout-dependent rejection: the
executable limits do.

## Supplied descriptor audit

The SIMISA block tree parses without an error.  The relevant fields are:

| Full token name | Value |
|---|---:|
| `terrain_nsamples` (140) | 256 |
| `terrain_sample_rotation` (141) | 0 |
| `terrain_sample_floor` (142) | -63 |
| `terrain_sample_scale` (143) | about 0.00195312 |
| `terrain_sample_size` (144) | 8 m |
| `terrain_sample_ybuffer` (146) | `-11ced50c_y.raw` |
| `terrain_patchset_distance` (160) | zero bits |
| `terrain_patchset_npatches` (161) | 8 |
| `terrain_patchset_patch` (164) count | 64 |

The 64 patch records are complete and internally regular:

- centers cover the 2,048 m footprint in 256 m steps;
- `RadiusM` is 128 m;
- shader indices are zero and valid for the two serialized shaders;
- the default texture coefficients `W` and `H` are `1/32`, matching the 32
  samples per patch;
- flags are zero and `ErrorBias` is 1.

The descriptor does not contain `terrain_sample_fbuffer` (145),
`terrain_sample_ebuffer` (147), `terrain_sample_nbuffer` (148),
`terrain_sample_cbuffer` (149), `terrain_sample_dbuffer` (150),
`terrain_sample_asbuffer` (281), or `terrain_sample_usbuffer` (282).  Their
absence does not trigger the dimension guard described below.  In particular,
the supplied tile does not exercise TSRE's opaque AS/US preservation.

## Exact MSTS rejection path

### Dynamic parse

`FUN_00710630` at VA `0x00710630` parses a `terrain_patchset` (159).  When it
reads `terrain_patchset_npatches` (161), it:

1. stores `P` in patch-set offset `+0x10`;
2. derives `R = terrain_nsamples / P` and stores it at `+0x08`;
3. derives hierarchy levels from `P` and `R`;
4. allocates and parses `P*P` `terrain_patchset_patch` (164) records.

This is why the custom file can pass syntax and patch-record parsing.  Dynamic
parsing is not the same as end-to-end acceptance.

### End-to-end limits

`FUN_006ee1c0` at VA `0x006ee1c0` merges the largest `R` from all patch sets
into terrain-manager offset `+0x68`.  The instructions at `0x006ee23b` reject
that manager value when it is greater than `0x10` (16).  It then merges
`terrain_nsamples` into manager offset `+0x6c`; the instructions at
`0x006ee253` reject it when it is greater than `0x100` (256).  Either branch
returns zero.

The initial terrain-loading path `FUN_006bdd50` independently repeats the
same checks:

- `0x006bde74`: reject maximum samples per patch greater than 16;
- `0x006bde83`: compare `terrain_nsamples` with 256 and reject when greater;
- failure path at `0x006bde9f`: unlink/free the partially loaded terrain and
  return zero.

The failure path does not emit a layout-specific diagnostic.  Therefore the
Route Editor can appear to close or crash silently even though the immediate
terrain failure is a deliberate rejection, not a demonstrated mesh-buffer
overflow.

## Tested-layout explanation

| Layout (`N / P`) | Samples per patch `R` | MSTS dimension result |
|---|---:|---|
| `128 / 16` | 8 | passes both limits; reported by the user to load in MSRE |
| `256 / 16` | 16 | passes both recovered limits |
| `256 / 8` | 32 | rejected: `R > 16` |
| `256 / 4` | 64 | rejected: `R > 16` |
| `512 / 16` | 32 | rejected: `N > 256` and `R > 16` |

This does not mean that MSTS requires exactly 256 samples and exactly 16
patches in every terrain file.  For example, `N=64`, `P=4` gives `R=16` and
passes these two guards.  That is compatible with small patch grids in
distant-terrain data.  Passing these guards is necessary, not by itself proof
that every arbitrary combination works.  The recovered hierarchy code also
expects coherent power-of-two dimensions, MSTS validates rounded
`terrain_sample_size` (144) as a power of two, and every referenced auxiliary
buffer must match `N` or `P`.

## What TSRE missed

The current TSRE `TerrainGridLayout::tryCreate` deliberately accepts
`terrain_nsamples` values through 2,048 and patch grids through 16 per side.
It checks divisibility and calculates `R`, but it has no legacy-MSTS target
check for `N <= 256` and `R <= 16`.  Consequently its `N=512, S=4` and
`N=1024, S=2` heightmap profiles, and its `N=256, S=8` tile with a 4x4 or 8x8
patch grid, can be valid TSRE layouts while being invalid MSTS layouts.

TSRE should distinguish at least two capabilities:

```text
TSRE/modern-engine layout support
legacy-MSTS layout support: N <= 256 && (N / P) <= 16
```

The second check should be a warning or a hard error whenever the requested
output target is MSTS.  It should not be used to remove TSRE's own dynamic
rendering support.

There is also a separate patch-metadata defect in `TFile::initNew`:

- `RadiusM` is correctly changed to half the physical patch size;
- `FactorY` remains hard-coded to `99.48125458` for every layout;
- `AverageY`, `RangeY`, and `FactorY` are not recomputed from changed height
  data before save.

MSTS uses `FactorY` as a bounding-sphere radius.  In the supplied flat 256/8
layout, `RadiusM=128`, so a conservative sphere must be at least
`sqrt(128^2 + 128^2)`, approximately 181.02 m, before adding any vertical
extent.  The serialized 99.48 m radius is too small and could cause premature
patch culling in an engine which accepts the layout.  It is not the present
failure cause because MSTS rejects `R=32` first.

The incorrect TSRE type for `terrain_patchset_distance` (160) is also not the
cause here: zero has identical all-zero bits as int32 and float32.

## What lies behind the guards

Bypassing only the samples-per-patch guard is unsafe.  The renderer has a
17-by-17 shared vertex cache, a fallback mesh allocated for a 16-by-16 patch,
and four 1,536-index batch thresholds.  All are exact consequences of the
current `R <= 16` ceiling.  A complete `R=32` candidate patch must resize or
raise these resources as well as changing both validation sites.

The detailed feasibility analysis, virtual-address inventory, 512-sample
assessment, and staged MSRE test plan were recorded in the earlier local report
`msts-r32-n512-patch-feasibility.md`, which is not retained in this repository.

## Recommended next tests

No Windows capture is needed to identify why the stock executable rejects the
three reported out-of-range layouts. Windows testing **is** required to
validate any experimental executable patch which raises those limits. For
stock-MSTS/TSRE compatibility testing, the most informative controls are:

1. `N=64`, `terrain_sample_size=32`, `P=4`, giving `R=16` and a 2,048 m tile;
2. the now-reported passing `N=128`, `terrain_sample_size=16`, `P=16`, giving
   `R=8`;
3. a deliberately invalid `N=256`, `P=8` control, which should be rejected by
   TSRE before launch when the target is MSTS.

For each fixture, include the `.t` and the referenced Y RAW so static payload
length, byte order, patch bounds, and height statistics can be checked before
any Windows run. The separate `R=32`/`N=512` executable-patch sequence was in
the unretained `msts-r32-n512-patch-feasibility.md` local report.
