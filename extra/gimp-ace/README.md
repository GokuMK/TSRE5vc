# GIMP 3 ACE exporter

Prebuilt Windows ZIPs are published under `gimp-ace-v*` on the repository's
[Releases page](https://github.com/GokuMK/TSRE5vc/releases). Extract and run
`install.cmd`, or follow the included manual installation instructions.
See [tool releases](../../docs/tool-releases.md) for packaging and publishing.

A native GIMP 3 plug-in using TSRE's unchanged ACE codec and the Qt-free
compatibility layer in `extra/common/ace-codec`. Windows and Linux share the
same C++17 sources and standalone CMake project. It does not build TSRE.

## Windows build and install

From the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File extra/gimp-ace/build.ps1 -Test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File extra/gimp-ace/install.ps1
```

The build script uses `C:\msys64\clang64` and changes PATH only within the
script. Builds are incremental. Override `-MsysRoot`, `-BuildDirectory` or
`-Jobs` as needed. The required MSYS2 CLANG64 packages are:

```sh
pacman -S --needed mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-libc++ \
  mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja \
  mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-gimp
```

The distributable is `build/file-tsre-ace.exe`. Copy it into a same-named
`file-tsre-ace` folder inside GIMP's plug-in directory. The installer detects
the installed GIMP version and uses `%APPDATA%\GIMP\<major.minor>\plug-ins`:
GIMP 3.2 uses `3.2`, while GIMP 3.0 uses `3.0`. For custom profiles, pass
`-PluginDirectory` with the plug-in root shown in GIMP's Preferences → Folders
→ Plug-ins. `-GimpRoot` selects a different GIMP installation.

Restart GIMP after installation, then **File → Export As**, enter a `.ace`
filename, and choose the ACE options. To uninstall, remove only the
`file-tsre-ace` directory from that profile.

No Qt or extra codec DLL is needed. The executable uses GIMP's own GTK, GEGL,
libgimp and C++ runtime DLLs. Tested with official Windows GIMP 3.2.4 and an
MSYS2 CLANG64 3.2.4 SDK. Compatibility with older GIMP/runtime distributions
requires testing against those distributions; a native binary is specific to
its OS and architecture. Never distribute the dialog-test executable.

## Linux build and install

Install CMake, Ninja, pkg-config, a C++17 compiler, and GIMP 3 development files
(including libgimpui, GTK 3 and GEGL). Arch's `gimp` package includes its headers;
on distributions that split development packages, install those too.

```sh
cmake -S extra/gimp-ace -B extra/gimp-ace/build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DGIMP_PLUGIN_DIR="$HOME/.config/GIMP/3.2/plug-ins"
cmake --build extra/gimp-ace/build-linux --parallel 4
ctest --test-dir extra/gimp-ace/build-linux --output-on-failure
cmake --install extra/gimp-ace/build-linux
```

Adjust the profile version/path to the one shown by GIMP. CMake installs only
the production executable under `file-tsre-ace/`. Without an explicit
`GIMP_PLUGIN_DIR`, installation uses `<prefix>/lib/gimp/3.0/plug-ins`, where
`3.0` is the plug-in ABI directory. Flatpak GIMP needs an SDK/runtime build and
installation compatible with its sandbox; these commands target native GIMP.

## Export behavior

- Exports the visible composite over the full image canvas, preserving offsets
  and transparency. Hidden layers are excluded. GIMP prepares a temporary RGB
  image when necessary; the working image, layers and precision are retained.
- Pixels are explicitly converted to **8-bit nonlinear sRGB, straight alpha**
  through GEGL/babl. This is intended for color textures. Raw numeric channels
  in linear/data textures are color-converted too; there is no raw-data mode.
- **Automatic** chooses RGB or RGBA from the prepared drawable's alpha channel
  capability. This can choose RGBA even when all pixels happen to be opaque.
- Supports all 14 codec encodings: `rgb`, `mask`, `rgba`, `rgb565`, `argb1555`,
  `argb4444`, `dxt1`, `dxt1mask`, `dxt2`, `dxt3`, `dxt4`, `dxt5`,
  `indexed-rgb`, `indexed-rgba`.
- **Suggested OR / MSTS formats only** hides packed, indexed and premultiplied
  formats. **Match source image format** selects opaque formats for RGB and
  full-alpha formats for RGBA. Turn it off to deliberately reduce/drop alpha.
  These filters only affect the dialog; scripts can select every encoding.
- Mipmaps require square power-of-two dimensions. Images are not resized.
  Indexed encoding requires at most 256 distinct colors across the mip chain;
  the encoder does not quantize. DXT and packed encodings lose precision.
- Zlib losslessly compresses the file envelope independently of pixel storage.
  Mipmaps and zlib default to off. GIMP remembers export settings.
- Limits: 16384 pixels per side and 64 million pixels in total. Pixels, encoded
  levels and output occupy memory simultaneously. Files are encoded completely
  before GIO replaces the destination; encoding failures preserve existing files.
- This is an exporter. It does not import ACE, retain authored mip levels,
  independent masks or arbitrary ACE metadata from another source.

## Scripting and tests

The PDB procedure is `file-tsre-ace-export`. Set `run-mode`, `image`, `file`,
`encoding` (default `auto`), `mipmaps` and `zlib` on its procedure config. Example
inside GIMP's Python console with an existing `image`:

```python
from gi.repository import Gimp, Gio
proc = Gimp.get_pdb().lookup_procedure("file-tsre-ace-export")
config = proc.create_config()
config.set_property("run-mode", Gimp.RunMode.NONINTERACTIVE)
config.set_property("image", image)
config.set_property("file", Gio.File.new_for_path("/path/to/texture.ace"))
config.set_property("encoding", "dxt5")
config.set_property("mipmaps", True)
config.set_property("zlib", True)
result = proc.run(config)
assert result.index(0) == Gimp.PDBStatusType.SUCCESS
```

CTest covers every encoding with/without mipmaps and zlib, buffer/dimension
bounds, indexed overflow, alpha defaults and recommendation filters. Integration
tests run GIMP in a disposable profile, then decode the files with the ACE codec
and verify composited pixels, transparency and color conversion:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File extra/gimp-ace/tests/smoke.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File extra/gimp-ace/tests/smoke.ps1 -Dialog
```

```sh
bash extra/gimp-ace/tests/smoke.sh extra/gimp-ace/build-linux
GDK_BACKEND=x11 xvfb-run -a bash extra/gimp-ace/tests/smoke.sh extra/gimp-ace/build-linux --dialog
```

The dialog test stages a separate test executable that exercises filters,
accept/cancel, and saves `export-dialog.png`. Its automatic responses are
compiled out of the production plug-in. Test profiles and outputs stay under
the selected build directory; the user's GIMP preferences are untouched.

References: [GIMP native plug-in tutorial](https://developer.gimp.org/resource/writing-a-plug-in/tutorial-c-basic/),
[ExportProcedure](https://developer.gimp.org/api/3.0/libgimp/class.ExportProcedure.html),
[export image preparation](https://developer.gimp.org/api/3.0/libgimp/method.ExportOptions.get_image.html).
