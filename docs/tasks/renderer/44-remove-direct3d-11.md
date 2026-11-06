# Task 44 - Remove the Direct3D backends

## Objective

Take QRhi's Direct3D 11 path out of TSRE, with the Direct3D 12 and Metal
leftovers, and stop shipping Direct3D's shader compiler
(`D3Dcompiler_47.dll`). The QRhi renderer runs on Vulkan (and, chosen
explicitly, on QRhi's OpenGL); the native OpenGL renderer stays for
systems without Vulkan.

## Why (user, 2026-11-05)

- Direct3D 11 was a performance disaster and has been off since
  2026-10-08 (`../renderer/20-qrhi-renderer.md`, Open): the setting no
  longer offers it, only `TSRE_RHI_API=d3d11` starts it. Its PBR program
  needs 21 fragment samplers where D3D11 allows 16, so glTF objects are not
  drawn, and it cost about twice Vulkan's CPU time (bbb's start view
  22.6 against 10.4 ms a frame).
- There is no benefit in working on it: old systems are served by the
  native OpenGL renderer, modern ones by Vulkan; Direct3D 12 or Metal would
  need a system to work on, and there is none now (Direct3D 12 does not
  come up with this Qt build, Metal is not created).

## What there is (2026-11-05)

- `RhiContext.cpp`: the `D3D11`, `D3D12` and `Metal` cases (creation, the
  shader targets HLSL 50 and MSL 12) and `TSRE_RHI_API=d3d11` / `d3d12`.
  The setting `core.rendering.rhiApi` already offers only Automatic, Vulkan
  and OpenGL.
- `RhiProgram.cpp`: HLSL as the default shader target, MSL for Metal.
- Workarounds for Direct3D 11 in the renderer:
  - `RhiRenderer.cpp`: the vertex index without the draw's base vertex
    (paged terrain's `terrainVertexBase`), and the Dynamic uniform arena
    copied whole (`copiesWhole`).
  - `RhiReflections.cpp`: a uniform buffer Dynamic because D3D11 takes no
    other kind.
  - `RhiShaderSource.cpp`: every fragment stage declares all vertex
    outputs (D3D11 links stages by register). Harmless on Vulkan and
    OpenGL; it can stay or go.
  - Comments naming Direct3D (`RhiRenderer.h` compute for the clouds'
    composite, `RhiUiDrawer.cpp` the y direction).
- `RhiShaderTestSuite.cpp`: cases baking HLSL 50.
- The release (`.github/workflows/release.yml`): windeployqt copies
  `D3Dcompiler_47.dll` into the package.

## Steps

1. Remove the Direct3D 11, 12 and Metal cases from `RhiContext` and
   `RhiProgram`, and the `TSRE_RHI_API` values for them; Vulkan and
   OpenGL remain, and an unknown value means the automatic choice.
2. Remove the workarounds above where they exist only for Direct3D 11;
   keep what Vulkan or OpenGL also needs (check each against both).
3. Bake shaders for SPIR-V and GLSL only; the `rhi-shaders` suite without
   the HLSL cases.
4. The release without the compiler DLL: windeployqt's
   `--no-system-d3d-compiler`; `opengl32sw.dll` (software OpenGL for
   machines without a driver) is a separate decision.
5. Docs: task 20's backend sections, `docs/settings-system.md` if it names
   Direct3D, the release notes.

## Verification

- Build; `rhi-shaders`, `settings` and the renderer captures (EUROPE1,
  Prudnik) on Vulkan the same images as before; QRhi on OpenGL starts.
- A release ZIP from the workflow without `D3Dcompiler_47.dll`: TSRE starts
  from a clean `PATH` on Vulkan and on the OpenGL renderer, and no module
  named `d3dcompiler` is loaded (the process's module list).
