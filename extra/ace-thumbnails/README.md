# ACE thumbnails for Windows Explorer

A standalone x64 MinGW COM thumbnail provider for MSTS/Open Rails ACE textures.
The distributable is **AceThumbnails.dll**. It requires no Qt, zlib or MinGW
runtime DLLs alongside it; its imports are Windows system libraries only.

The project compiles the repository's `src/tsre/texture/AceDocument.cpp` and
`DxtCodec.cpp` unchanged. It does not build TSRE or change its CMake configuration.
Its build directory is separate, and subsequent builds are incremental.

## Build and test

From the repository root, using the existing local MinGW/CMake/Ninja installation:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File extra/ace-thumbnails/build.ps1 -Test
```

The execution-policy option applies only to that PowerShell process. The script
accepts `-Toolchain`, `-CMake`, `-Ninja`, `-BuildDirectory` and `-Jobs` overrides.
It temporarily adds the compiler's `bin` directory to its own environment so
GCC subprocesses can resolve their runtime DLLs, including during initial CMake
compiler probes. It restores PATH afterwards and never changes system PATH.
No Qt SDK is needed for the normal build, despite this machine's tools living
under `C:\Qt6\Tools`.

Output: `extra/ace-thumbnails/build/AceThumbnails.dll`.
Only that DLL needs to be copied to the installation directory. The test EXEs
and static libraries are development artifacts.

Equivalent configuration in a shell with the MinGW compiler available:

```powershell
cmake -S extra/ace-thumbnails -B extra/ace-thumbnails/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build extra/ace-thumbnails/build --parallel 4
ctest --test-dir extra/ace-thumbnails/build --output-on-failure
```

For a DLL-only build, configure with `-DBUILD_TESTING=OFF`.

## Test without Explorer registration

The test harness loads the real DLL through `LoadLibrary`, obtains its COM class
factory, initializes it with an `IStream`, and inspects the returned DIB. It does
not need Explorer registration. Registration tests redirect HKCU within the test
process to a newly created temporary key, test restoration of the previous handler,
and remove that key afterwards. The actual `.ace` association is untouched.

Tests cover all 14 codec encodings with and without zlib/mips, binary-buffer
semantics, COM identity/reference counts, rectangular images, channel order,
premultiplied alpha, area filtering, partial reads, stream errors, truncation,
mutated headers and resource limits. A second CTest checks DLL dependencies.
Strip decoding is compared with full-frame area filtering, including odd sizes,
partial DXT blocks and planar mip tails. Large DXT1/DXT5 fixtures verify 8192
support through the real DLL without allocating full-size RGB(A) test images.

To render an actual file to a BMP with a checkerboard behind transparent pixels:

```powershell
& .\extra\ace-thumbnails\build\ace_thumbnail_tests.exe `
  "$PWD\extra\ace-thumbnails\build\AceThumbnails.dll" `
  --render 'C:\textures\example.ace' "$PWD\preview.bmp" 256
```

Paths support Unicode. The optional size is 1..1024; the default is 256.
The self-test also writes `thumbnail-preview.bmp` in its working directory.
`--render` reports elapsed extraction time and the test process's peak working
set. Use `--shell-render` instead to force extraction through Windows'
registered thumbnail cache/provider and notify Explorer to refresh that file.
This mode requires an installed provider; its memory report covers the test
client, not the separate Windows thumbnail host. It does not clear other cached
thumbnails or change the ACE file.

An optional Qt oracle checks the compatibility layer against real Qt:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File extra/ace-thumbnails/build.ps1 -Test -QtParity
```

Use `-QtPrefix` to select a different matching MinGW Qt SDK. This builds a separate
Qt-based executable; the DLL remains Qt-free. Each implementation generates 224
ACE files which the other reads. Decoded pixels, masks, dimensions and metadata
must agree. Compressed byte streams may differ between miniz and Qt's zlib.
Once enabled, parity remains enabled in that build's CMake cache; CTest supplies
its Qt runtime search path automatically. Disable it with
`-DACE_THUMBNAILS_QT_PARITY=OFF` when configuring.

## Install for the current Windows user

Build/test does **not** register the DLL. First copy `AceThumbnails.dll` to a stable
directory where it will remain installed. From a normal 64-bit PowerShell window:

```powershell
& "$env:SystemRoot\System32\regsvr32.exe" 'C:\Tools\AceThumbnails\AceThumbnails.dll'
```

Registration writes only the current user's `Software\Classes` keys, so elevation
is unnecessary. It registers the `.ace` thumbnail handler and its COM class with
`ThreadingModel=Apartment`. It does not change the default application for ACE
files or disable thumbnail process isolation.

An existing per-user thumbnail handler is saved and restored on uninstall.
Machine-wide registrations are not edited. Explorer is notified of the association
change. Use a thumbnails view (for example, Large icons) to check the result;
already cached thumbnails can remain visible until Windows refreshes them.

Uninstall using the same DLL:

```powershell
& "$env:SystemRoot\System32\regsvr32.exe" /u 'C:\Tools\AceThumbnails\AceThumbnails.dll'
```

Uninstall restores the previous per-user handler only if this DLL's handler still
owns the association. If another provider has replaced it, that association is
preserved. Unregister before moving/removing the installed DLL. Windows can keep
an installed DLL loaded until its Explorer/thumbnail host releases it.

CLSID: `{63CE2D66-42B2-42EB-8CE5-6F7B4CC9C632}`.

## Implementation and limits

- `src/Provider.cpp`: `IThumbnailProvider`, `IInitializeWithStream`, class factory
  and DLL lifetime. C++ exceptions are translated to HRESULTs at the COM boundary.
- `src/Thumbnail.cpp`: bounded stream reading, ACE decoding, mip selection and
  alpha-aware area scaling. Returns a top-down 32-bit premultiplied BGRA DIB.
  Aspect ratio is preserved to integer-pixel rounding; images are never enlarged.
- `src/Registration.cpp`: per-user registration and previous-handler restoration.
- `../common/ace-codec/qt_compat/`: shared standard-library replacements for the small Qt subset the
  codec uses. `QByteArray` owns its bytes and makes eager copies, including
  `fromRawData`; Qt-style implicit sharing is unnecessary for correctness here.
  `QString` carries error text. `QFile`/`QSaveFile` deliberately fail; Explorer
  provides a stream. `qCompress` uses bundled miniz and Qt's four-byte size prefix.

Provider limits: 384 MiB input/inflated ACE body, 8192 x 8192 base-level pixels,
8192 pixels per source dimension, and 1024 pixels per output dimension.
The byte limit accommodates an 8192 planar RGBA + mask image with all mipmaps.
Larger inputs fail cleanly. Parsing still validates all mip payloads; decoding
uses the smallest existing mip that is large enough for the requested thumbnail.

`AceDocument::decodeThumbnail` decodes DXT in four-row strips and other formats
one row at a time, using the existing decoder. It produces premultiplied BGRA
with area filtering, without a full-size RGB(A) allocation. At width 8192, decoded
scratch is at most 128 KiB, plus encoded strip bytes, one accumulator row and
the output thumbnail. The parser uses a non-owning view for the uncompressed
input body and the provider releases the input buffer before pixel decoding.
It still retains encoded payloads and inflates zlib envelopes in full: total
memory remains proportional to encoded/inflated file size, not just the thumbnail.

Example measured on this development machine: an 8192 DXT1 file without mipmaps
(`fraktal.ace`, 32 MiB) produces a 256-pixel thumbnail in about 0.64 seconds,
with about 71 MiB peak process working set in the direct DLL harness. This is
a single-file measurement, not a guarantee for other formats or Explorer hosts.

The compatibility types are private to this build. Never link Qt-built codec
objects into the same DLL. A future static-Qt backend can compile the same codec
sources against Qt after removing the compatibility include path/implementation;
the Explorer interfaces do not depend on the backend's types.

API references: [Microsoft's thumbnail provider guidance](https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers),
[GetThumbnail contract](https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nf-thumbcache-ithumbnailprovider-getthumbnail),
[Qt compression format](https://doc.qt.io/qt-6/qbytearray.html#qUncompress).
