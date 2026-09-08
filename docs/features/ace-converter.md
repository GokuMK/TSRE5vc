# Ace Converter

Ace Converter opens ACE, DDS and Qt-supported image files, previews their base
image, and exports ACE or a format supported by the installed Qt image writers.
It operates on full-resolution CPU pixels and does not need a route, game assets
or an OpenGL context.

## Launch and GUI

```powershell
.\build\TSRE5vc.exe --aceconv
.\build\TSRE5vc.exe --aceconv --file "C:\Textures\wall.dds"
```

GUI launches use TSRE's normal settings/profile and palette initialization,
including the built-in dark palette, system-theme mode and its configurable
accent. `--profile`, `--settings`, `--appdata-profile` and `--set` work as for
the other TSRE windows. The converter still skips game-asset loading.

The left panel provides the source picker and labeled, read-only fields for
filename, dimensions, format, pixel storage, ACE zlib compression, alpha, mip count
and independent mask. ACE zlib shows Yes/No for ACE and Not applicable for other inputs.
Both side panels use TSRE's coloured main labels. The center
shows transparency over a checkerboard, with native-size scrolling and Fit/100%
actions. The right panel provides ACE encoding, mipmaps and zlib compression,
with separate ACE export and Image export sections. File dialogs confirm replacement of existing
files. Import and export run on a worker thread; an unsuccessful import leaves
the previous source available.

Two independent ACE-format filters are on by default:

- **Suggested OR / MSTS formats only** shows RGB, RGBA, RGB + mask, DXT1,
  DXT1 with alpha, DXT3 and DXT5. Turn it off to expose packed, indexed and
  premultiplied formats. This is a conservative authoring shortlist, not a
  claim that every hidden format is unsupported.
- **Match source image format** hides alpha encodings for an RGB source and
  opaque encodings for an alpha source. Full-alpha sources exclude 1-bit-alpha
  formats; mask sources allow both binary and full alpha. Source channel
  capability matters even when all the current pixels are opaque.

The source-aware suggestion appears first and is selected when loading an image.
DDS/ACE DXT5 suggests DXT5, including opaque DXT5 images. DXT2/4 suggest DXT3/5
under the OR/MSTS filter; turning that filter off makes the original formats
available. A still-visible user selection survives filter changes. Disabling
both filters restores all ACE encodings. These GUI filters do not restrict
explicit console `--ace-format` values.

## Console conversion

Providing `--output` converts one file and exits without creating a window:

```powershell
.\build\TSRE5vc.exe --aceconv --file "wall.png" --output "wall.ace" --ace-format dxt5 --mipmaps --zlib
.\build\TSRE5vc.exe --aceconv "wall.ace" --output "wall.png"
.\build\TSRE5vc.exe --aceconv --file "wall.dds" --output "wall.ace" --ace-format rgb --overwrite
.\build\TSRE5vc.exe --aceconv --help
```

Relative paths use the caller's working directory. A console `--aceconv --output` launch
bypasses TSRE settings/profile initialization, startup-args.txt and asset loading.
The older startup-args.txt `--aceconv --file ...` selection also opens the GUI
through the normal TSRE initialization path; use terminal arguments for console
conversion.

`--output` selects the writer by extension. ACE arguments are rejected for other
outputs. Existing console outputs require `--overwrite`. All writes are atomic:
an encoding or write failure does not truncate the existing destination.

Exit codes: 0 for success/help, 1 for conversion or I/O failure, 2 for invalid
arguments. Completion goes to stdout and errors to stderr.

## ACE options

| `--ace-format` | Pixel storage |
| --- | --- |
| `rgb` | Planar RGB, no alpha |
| `rgba` | Planar RGB, 8-bit alpha and a mask |
| `mask` | Planar RGB and a 1-bit mask |
| `dxt1` | DXT1 opaque |
| `dxt1mask` | DXT1 with binary alpha |
| `dxt3`, `dxt5` | DXT3 explicit alpha / DXT5 interpolated alpha |
| `rgb565`, `argb1555`, `argb4444` | Packed 16-bit color, with zero/one/four alpha bits |
| `dxt2`, `dxt4` | Premultiplied variants of DXT3/DXT5 |
| `indexed-rgb`, `indexed-rgba` | Indexed color with RGB/RGBA palette |

Without an explicit console format, the converter uses RGBA when the image contains
nonopaque pixels and RGB otherwise. `--mipmaps` and `--zlib` default to off.
Zlib compresses the file envelope losslessly, independently of pixel encoding.

Mipmaps require square, power-of-two dimensions. The converter does not resize
images to satisfy this requirement. Indexed export requires at most 256 distinct
colors across the base image and generated mip chain; it does not quantize colors.
DXT and packed formats lose color/alpha precision. See the
[ACE library notes](ace-library.md) for codec quality, mip filtering and native
MSTS/MSRE compatibility qualifications for premultiplied and indexed alpha formats.

## Import and preservation limits

- ACE import uses `AceDocument`; DDS uses `DdsLib::loadImage`, which shares the
  existing `DxtCodec` implementation. Ordinary images use `QImageReader` with
  orientation metadata applied. Available ordinary formats depend on Qt plugins.
- DDS supports legacy 2D DXT1–5 and 16/24/32-bit RGB with channel masks, including
  padded rows. It reports unsupported DX10 formats, cubemaps and volume textures.
  Import validates the base payload and reports the declared mip count; it does
  not decode or validate unused lower DDS mip payloads.
- Images are limited to 16384 pixels per side and 64 million pixels overall.
  ACE and DDS readers additionally enforce a 512 MiB file limit.
- Preview and conversion use the base image. ACE output re-encodes pixels and
  regenerates requested mipmaps. It does not retain authored lower mip levels,
  arbitrary header metadata or embedded resources.
- Independent ACE masks are passed to the ACE writer. Standard image exports
  use decoded alpha; an independent mask is not multiplied into a separate alpha
  channel. Some output formats, such as JPEG, cannot retain transparency.

## Developer verification

`tsre_ace_converter_tests` exercises the shared backend, DDS validation, real
command-line conversions and the window. CTest invokes it against `TSRE5vc`.

```powershell
cmake --build build --parallel 4
ctest --test-dir build -R ace_converter --output-on-failure
```

CTest's UI checks use Qt's offscreen platform plugin. If that plugin is not in
the deployed plugin directory, set `QT_PLUGIN_PATH` to the matching Qt SDK's
plugins directory for the test process. This is only needed for the GUI test;
ordinary raster console conversion uses `QCoreApplication` and requires no GUI
platform. Qt's optional SVG reader additionally requires `Qt6Svg` and a GUI
platform for its font database, even though conversion creates no window or
OpenGL context. SVG console input uses `QGuiApplication` for that reason.
An optional `--snapshot PATH.png` argument to the test executable saves a window
image for visual review. Keep the existing build directory and configuration so
these builds remain incremental.
