# Renderer measurements on Windows hardware

PowerShell scripts (Windows PowerShell 5.1) for measuring and debugging the
renderers on a GPU, written for the Steam Deck under Windows (task 25,
`docs/tasks/renderer/25-qrhi-hardware-investigation.md`). They start the
editor themselves and need no administrator rights.

| Script | Does |
|---|---|
| `Measure-Frames.ps1` | Frames a second, frame times, CPU, GPU load and memory of one run (PresentMon, performance counters). |
| `Compare-Variants.ps1` | Runs variants alternately, several times each; optionally load times. |
| `Trace-Run.ps1` | One run with `TSRE_RHI_TRACE=1` (or other variables); keeps the log, prints load times. |
| `Capture-Views.ps1` | Captures a cases file's views and compares two captures (as `scripts/renderer-parity.sh`). |
| `Screenshot-Editor.ps1` | Screenshot of the editor window, overlays included. |
| `Capture-Frame.ps1` | One RenderDoc capture of the editor. |
| `Inspect-Capture.ps1` | Reports on a capture: `draws` (state, VS output, pixel history), `resources` (textures and buffers by size). |

Output goes to `build\hardware\` (ignored by git). Settings go in as
`-Set key=value,...` (`--set` of TSRE), environment variables as
`-EnvVars @{ NAME = 'value' }`; they apply to the started TSRE only.

To measure or capture at a chosen spot, start the editor there:
`core.startup.camera=tileX,tileZ,x,y,z,yaw,pitch` (tile and position as in
the navigation window, angles in degrees). **Tools > Copy Camera Position**
copies the current camera as that `--set` argument.

```powershell
cd scripts\hardware
$common = 'core.rendering.backend=qrhi', 'core.interface.hud.showEditorFps=true'
.\Measure-Frames.ps1 -Label vk -GameRoot C:/trainsim -Route bbb -Set ($common + 'core.rendering.rhiApi=vulkan')

$variants = [ordered]@{
    vulkan = @{ Set = @('core.rendering.rhiApi=vulkan') }
    opengl = @{ Set = @('core.rendering.rhiApi=opengl') }
}
.\Compare-Variants.ps1 -Variants $variants -GameRoot C:/trainsim -Route bbb -Set $common -Repeats 3 -LoadTimes
```

## Tools

Paths default to `C:\Qt6` and `C:\dev\tools`; environment variables
override them: `TSRE_QT_ROOT`, `TSRE_MINGW_ROOT`, `TSRE_BINARY`,
`TSRE_HARDWARE_OUT`, `TSRE_PRESENTMON`, `TSRE_QRENDERDOC`. Nothing goes on
the system `PATH` or into the registry.

- PresentMon 2.6 console (`PresentMon-2.6.0-x64.exe`, GitHub
  GameTechDev/PresentMon). It traces ETW without administrator rights for
  members of the Performance Log Users group.
- RenderDoc 1.46 portable zip. Two changes to the unpacked folder:
  - Rename `qrenderdoc.exe` to `qrenderdoc-script.exe`. RenderDoc's crash
    handler (`renderdoccmd.exe`, still needed for injection) starts
    `qrenderdoc.exe` to show its crash reporter; renamed, it cannot.
  - Copy `%APPDATA%\qrenderdoc\UI.config` and `analytics.json` to
    `%APPDATA%\qrenderdoc-script\` (the renamed program keeps its settings
    there), or start it once by hand and answer the first-run questions;
    otherwise scripts wait on them.
- Vulkan validation layer without the Vulkan SDK: MSYS2's clang64 package
  `mingw-w64-clang-x86_64-vulkan-validation-layers` with the
  `libSPIRV-Tools.dll`, `libSPIRV-Tools-opt.dll`, `libc++.dll` and
  `libunwind.dll` of its dependencies, in one folder. Use it with
  `-EnvVars @{ TSRE_RHI_DEBUG = '1'; VK_ADD_LAYER_PATH = '<folder>' }`;
  messages go to `log.txt` as `vkDebug:`.

## Notes

- RenderDoc's Vulkan layer is enabled for TSRE only (`VK_ADD_LAYER_PATH`,
  `VK_INSTANCE_LAYERS`); in qrenderdoc's own environment it crashes it, as
  Qt 6's plugin paths do. The Windows loader of the AMD driver ignores
  `VK_ADD_IMPLICIT_LAYER_PATH`. RenderDoc refuses Vulkan instances that
  enable `VK_KHR_portability_enumeration`; TSRE leaves it out outside macOS.
- RenderDoc environment changes to `PATH` do not reach the started program
  (the Windows variable is `Path`), so the scripts append the Qt folders to
  qrenderdoc's own `PATH`, after its Qt 5.
- The FPS display counts the editor's timer ticks; PresentMon counts
  presented frames. The editor timer (15 ms, `core.system.fpsLimit`) caps
  both at about 66 a second.
- Clocks of an APU drift with temperature and power: compare variants with
  `Compare-Variants.ps1`, not runs taken minutes apart.
- Take both captures of a comparison with the same profile and build of
  the content: profile settings (time of day, lights, shadows) change the
  images.
- Pixel history in `Inspect-Capture.ps1 -Script draws` looks at the first
  matching draw with a vertex on screen, which may be in another pass
  (water reflection, environment faces); narrow it with `RDC_MIN_WIDTH`
  or `RDC_EVENT`.
- In PowerShell, `$Args` is an automatic variable: never use it as a
  parameter name.
