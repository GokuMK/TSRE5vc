# 05 - DDS with the DX10 header, BC7 and the other block formats

Status: planned (the user, 2026-11-10), not started; for later.

## Why

Since 2026-11-10 TSRE picks a texture's file as Open Rails does: the `.dds`
of an `.ace` or `.dds` name when there is one, else the `.ace`
(`ContentPath::textureSource`, `docs/features/textures-and-texlib.md`). Newer
Open Rails content made with modern tools ships `.dds` files TSRE cannot
read yet: the DX10 header (`DDS_HEADER_DXT10`, a DXGI format code), which
such tools also write for plain BC1 to BC3, and the block formats behind it
(BC7 above all, BC4 and BC5 for single and two channel maps, BC6H for HDR).
Today such a file fails, and TSRE loads its `.ace` in its place when there
is one (`loadAceForUnreadableDds` in `TexLib.cpp`, logged once a file);
without an `.ace` the texture is missing.

## Where things stand

- `DdsLib` (`src/tsre/texture/DdsLib.cpp`): legacy headers only; DXT1, DXT3
  and DXT5 kept as blocks, uncompressed 24 and 32-bit decoded. An unknown
  FourCC (`DX10`, `ATI1`, `ATI2`, `BC4U`, `BC5U`...) marks the texture
  missing.
- `DdsImage.cpp` (the CPU-only reader for image tools) rejects DX10
  outright ("DX10 DDS formats are not supported").
- GPU: QRhi uploads BC1 to BC3 (`RhiTextures.cpp`, BC1 support checked at
  startup; DXT1 with alpha uploaded as BC3). No BC4, BC5, BC6H or BC7 yet.
- CPU decode (`DxtCodec`): DXT1, DXT3, DXT5, for editing, thumbnails,
  readback and the native OpenGL renderer.
- On this machine (2026-11-10): 5,927 `.dds` files, all DXT1/3/5 or
  uncompressed (none with the DX10 header): nothing to see yet; the gap is
  for other content.
- BC7 encoding was benchmarked for on-load compression (task 04,
  "Compression benchmark"): a different job (encoding), but its GPU upload
  is the one this task adds.

## Plan

1. **Read the DX10 header** in `DdsLib` and `DdsImage`: after the legacy
   header when the FourCC is `DX10`, the DXGI format, the resource
   dimension (2D only; arrays and cube maps rejected with a message), the
   array size (1). Map DXGI formats: BC1-BC3 (`_UNORM` and `_UNORM_SRGB`) to
   the paths there are; BC4, BC5, BC6H (UF16), BC7 (and `_SRGB`) to new
   ones; `R8G8B8A8`/`B8G8R8A8` to the uncompressed path. Legacy FourCCs
   `ATI1`/`BC4U` and `ATI2`/`BC5U` too.
2. **GPU upload of the new blocks** through QRhi (`QRhiTexture::BC4`,
   `BC5`, `BC6H`, `BC7`), each checked with `isTextureFormatSupported`
   once; where one is not supported, decode on the CPU and upload RGBA8
   (memory and time logged). The Deck's AMD GPU takes all of them under
   Vulkan. sRGB: as the BC1-BC3 paths treat it today (check how the
   shaders expect colour textures).
3. **CPU decode** for what needs pixels (editing, thumbnails, readback, the
   native OpenGL renderer, mip regeneration): `bcdec` (Sergii Kudlai,
   single header, public domain or MIT) for BC4 to BC7, behind `DxtCodec`.
   A third-party file: the user decides before it is added.
4. **Keep the `.ace` fallback** for files still unreadable (corrupt, a
   format left out such as BC6H if not done, a texture array).
5. **Mipmaps:** the file's own levels kept as blocks; the shape-texture
   mipmap settings (`core.rendering.shapeTextures.*`) "regenerate" and
   "improve" decode, make the levels and upload uncompressed, as for DXT
   today (or skip for BC6H/BC7: to decide).

## Tests

- Fixture files (small, made in the test from known pixels where a
  writer exists; else a few bytes of known BC7 blocks checked in): DX10
  BC1, BC3, BC4, BC5, BC7, BC7 sRGB, R8G8B8A8; legacy ATI1, ATI2.
- Each: loads, size and mip count right, decoded pixels against the
  reference within the format's error, uploaded as blocks where supported.
- A DX10 cube map or array: rejected with a message, the `.ace` loaded if
  there is one.
- `texture-readback`, `texture-residency`, `content-path` suites still
  pass; a capture with a BC7 texture in a shape, against the same texture
  as BC3.

## Open

- BC6H (HDR): content uses it rarely for shapes; it needs a float upload
  path on the CPU fallback.
- The ACE converter (`tsre_ace_tool`) could then convert to and from DX10
  DDS; BC7 encoding is task 04's question (encoder, time).
