# Task 02 - Texture Memory Stats and Debug Dump

## Objective
Make it easy to compare RAM/VRAM impact of texture pipeline changes (compressed upload, CPU decode removal, etc.) without relying on external profilers.

## Scope
- Add lightweight per-texture accounting helpers on `Texture`:
  - CPU bytes currently resident (decoded pixels + encoded blocks).
  - estimated GPU bytes based on the internal format used at upload time.
- Add a `TexLib::dumpStats()` debug helper that prints:
  - texture counts (total/loaded/glLoaded/editable/missing/error)
  - approximate CPU pixels MB, CPU encoded MB, and estimated VRAM MB
  - number of GPU textures that were uploaded in a compressed format
- Add a hotkey to trigger the dump in tools:
  - Route Editor: `Ctrl+Shift+F10`
  - Shape Viewer: `Ctrl+Shift+F10`

## Notes / Caveats
- VRAM is **estimated**, not measured from the driver.
  - It is derived from texture dimensions + the internal format used when uploading.
  - Mipmaps are not currently accounted for (most TSRE usage is non-mipmapped today).
- The dump is a debugging aid and is not designed to be thread-safe while textures are still being populated by loader threads.

## Touch Points
- `src/tsre/texture/Texture.h/.cpp` (accounting + track `gpuInternalFormat`)
- `src/tsre/texture/TexLib.h/.cpp` (`dumpStats`)
- `src/routeEditor/RouteEditorGLWidget.cpp` (hotkey)
- `src/shapeViewer/ShapeViewerGLWidget.cpp` (hotkey)

## Acceptance Criteria
- Pressing `Ctrl+Shift+F10` prints a single-line summary to the debug output.
- The summary reports non-zero `glCompressed` counts when DXT textures are uploaded in compressed form.
- No behavior change for normal texture load/upload (stats are read-only).

