# MSTS Bin 1.8 -> 1.9 custom-terrain patcher

This package patches an existing, legally obtained `train.exe`;
it does not contain or distribute the original executable.

The mandatory patch raises the terrain limits to:

```text
terrain samples per side (N): up to 1024
patches per side (P):         up to 32
samples per patch (R=N/P):    up to 32
```

It also updates the Route Editor height-edit bitmap and seam handling for N1024 and relocates both 4,096-entry visible-patch lists into a new writable PE section with 16,384 entries each.

The script is self-contained, uses only the Python standard library, and is locked to this unmodified, non-widescreen MSTS Bin executable:

```text
Version:    MSTS Bin 1.8.052113
Size:       4,091,953 bytes
SHA-256:    69218fce876298c684a2140c7d3925a452c47bb10037ffd8c491f65c5c0c6e7a
```

It will reject every other executable, verify each complete instruction before changing it, verify the complete generated file against its known SHA-256, and write a separate output. It never replaces the input executable.

## Requirements

- Windows with Python 3.9 or later (`py` or `python`), or another system with Python 3.9 or later.
- The exact clean MSTS Bin 1.8.052113 `train.exe` identified above.

No assembler, compiler, binary patch utility, or Python package is needed.

## Recommended command

For the complete test build—including direct route launch and the corrected, editor-guarded 1280x800 Route Editor window, render target, and mouse mapping—open Command Prompt in the patcher's directory and run:

```bat
py patch_msts_bin_1_9-alpha.py "C:\MagiPacks\Microsoft Train Simulator\train.exe" "C:\MagiPacks\Microsoft Train Simulator\train.terrain-test.exe" --full
```

The expected output is:

```text
Size:       4,366,336 bytes
SHA-256:    13e1104718d5f0da78deb3f334e37828424c31937be237e99d49228fe7ef9a8f
```

The script only creates the file. Preserve the installed `train.exe`, then manually copy or rename the generated executable as desired. Running or installing it is also a separate manual action.

If the output argument is omitted, the patcher creates
`train.terrain-patched.exe` beside the input. Existing outputs are refused;
`--force` permits replacing the output file, never the source.

## Profiles

| Command options | Added conveniences | Size | Output SHA-256 |
|---|---|---:|---|
| none | terrain only | 4,358,144 | `810ac9d4c21eb493ad9172ffbb499b25ea9bba13ef09e982b3a1f8e5812eaf19` |
| `--direct-route` | `-editroute:ROUTE_FOLDER` | 4,362,240 | `5f5f398bb7abf6a58e2609b945f01d7df12e52cdaaed417255785bd3cce328ce` |
| `--window-1280x800` | editor-guarded window/render/mouse size | 4,362,240 | `484e534521a25b46d680a46871852d8653096daeae91b295e750d00d45ac4eeb` |
| `--full` | both conveniences | 4,366,336 | `13e1104718d5f0da78deb3f334e37828424c31937be237e99d49228fe7ef9a8f` |

The `--direct-route` output reproduces the byte-exact terrain build previously assembled by the development patch chain. The embedded direct-route payload is the same validated payload; embedding removes the old GNU assembler/linker dependency.

The current window profiles add two guarded request stubs in a separate
`.editwin` section. When MSTS's existing editor-mode flag is clear, they pass the original 640x480 values. They request 1280x800 only in editor mode. The new profiles are statically verified and await Windows confirmation of both MSRE operation and restored cab-view behavior.

## Terrain layouts and current test status

The one patched executable retains stock terrain support and admits the tested
custom layouts:

```text
N=512,  P=16, R=32
N=512,  P=32, R=16
N=1024, P=32, R=32
```

Manual testing has confirmed rendering for both P32 layouts and two P32 tiles visible together. N512/P16/R32 height editing was also confirmed.

Custom `.t` files intended for MSRE editing must name
`terrain_sample_ybuffer` (146), `terrain_sample_ebuffer` (147), and
`terrain_sample_nbuffer` (148). Missing E/N payloads can be regenerated, but the resource entries must exist. A route should also contain a sound-source world object to avoid the unrelated known Windows 10 Route Editor 1–2 FPS bug.

Current TSRE supports P32 selection by assigning selection colours to the 256 patches closest to the camera and mapping those temporary colours back to the actual patches. Consequently, it can select patch records above 255, although only the nearest 256 patches participate in a selection pass. Open Rails still requires corresponding source changes to render these nonstandard layouts.

## Direct route launch

When `--direct-route` or `--full` is used, launch a route by its folder
identifier with MSTS's native colon separator:

```bat
train.exe -editroute:terrainsize
```

An absent or unknown route keeps the ordinary chooser behavior.
