# Ace Converter

Status: implemented and verified on 2026-09-08. Usage and format qualifications:
[Ace Converter](../../features/ace-converter.md).

## Goal

Implement the existing `--aceconv` launch mode as a standalone texture converter
inside TSRE, with a GUI and a console interface sharing the same conversion code.
Conversion must use full-resolution CPU pixels and work without game content or
an OpenGL context. Keep development builds incremental.

## GUI

- Launch with `TSRE5vc.exe --aceconv`, optionally followed by `--file PATH`
  or a single positional input path.
- Use the same TSRE profile, palette and main-label colours as other TSRE windows.
  Keep console conversion independent of settings and GUI initialization.
- Left panel: source filename, open-file button, dimensions, format, pixel
  encoding/compression, transparency and mip count in labeled read-only input
  boxes, plus relevant import warnings.
- Center: texture preview over a transparency checkerboard, with scrollbars at
  native size, a 100% action and a fit-to-view action.
- Right panel: ACE pixel format, mipmap generation, zlib envelope compression,
  an Export ACE save dialog and a separate Save image as dialog. Separate ACE
  and ordinary image export with coloured section labels.
- Offer two independent format filters, enabled by default: common OR/MSTS
  choices and choices matching the source's alpha/encoding. RGB hides alpha
  formats; DDS DXT5 suggests DXT5 even with opaque pixels. Keep all formats
  accessible with filters disabled. Avoid DXT2/4 and indexed-alpha recommendations
  under the common OR/MSTS filter, respecting the library's native-alpha findings.
- Keep image loading and encoding off the GUI thread. Show completion/errors;
  a failed import must preserve the previously loaded image.
- Explain alpha loss, binary alpha and indexed-color restrictions. Disable
  mipmap generation for images that are not square and power-of-two sized.

## Input and output

Input:

- ACE through `AceDocument`, including compressed envelopes and independent masks.
- DDS through a bounded CPU image API in the DDS library. Reuse `DxtCodec` for
  DXT decoding. `Texture::setEditable()` already supports CPU decoding of retained
  DDS blocks; no new block decoder is needed.
- Formats supported by the installed Qt image-reader plugins.

Output:

- ACE formats exposed by `AceWriteOptions`: RGB, RGBA, RGB plus mask, DXT1 opaque
  or masked, DXT2/3/4/5, RGB565, ARGB1555, ARGB4444 and indexed RGB/RGBA.
- Formats supported by the installed Qt image-writer plugins.

Conversion uses the base image. Requested ACE mipmaps are regenerated; authored
lower levels and arbitrary source resources/metadata are not preserved. ACE
independent masks remain available to the writer. Indexed export requires no
more than 256 distinct colors; quantization is outside this task.

DDS import covers ordinary 2D legacy DXT1-5 and 16/24/32-bit RGB bit-mask formats,
including padded rows. Unsupported DX10 formats, cubemaps and volume textures
must fail with an explanation. Bounds and truncated-payload checks must happen
before reading pixels or allocating their decoded storage.

## Console contract

```text
TSRE5vc.exe --aceconv --file INPUT --output OUTPUT
    [--ace-format FORMAT] [--mipmaps] [--zlib] [--overwrite]
```

- `--output` selects console conversion with no window. Raster conversion does
  not require a GUI platform; Qt's optional SVG reader needs one for its fonts.
- Output extension selects the writer. ACE options are accepted only for ACE output.
- Default ACE encoding is RGBA when pixels contain transparency, otherwise RGB.
  Mipmap generation and zlib compression default to off.
- Resolve relative paths against the caller's working directory.
- Existing outputs require `--overwrite`; write atomically so a failed conversion
  does not truncate an existing file.
- Report diagnostics on stderr, completion on stdout. Exit 0 on success, 1 for
  conversion/I/O failures, and 2 for invalid command-line arguments.
- `--aceconv --help` lists options and pixel-format names.

## Verification

- Build TSRE and the converter tests incrementally with the existing Debug toolchain.
- Exercise Qt-image to ACE to PNG conversion, ACE modes, transparency/masks,
  odd-width images, mip constraints and failed-write preservation.
- Test DDS DXT1/3/5 plus supported packed RGB formats, padded rows, truncation,
  oversized dimensions and unsupported surface types.
- Exercise CLI relative paths, overwrite handling, invalid options and operation
  without a valid GUI platform plugin.
- Render and inspect the GUI with a representative texture, verify scrolling,
  fit/native-size actions and export-option availability.

Validation completed with the existing Debug/MinGW build: 166 converter and
snapshot checks (including both GUI save dialogs, palette inheritance, independent
source/simulator filters, opaque DXT5 suggestions and console conversions against
TSRE), all passing. Dark and light palette renders were visually inspected.
The underlying ACE codec suite also passed its 7,539 checks during initial integration.
