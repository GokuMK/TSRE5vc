# Task 01 - Runtime Pipeline Switch

## Objective
Replace manual comment-based toggling between old and new frame paths with a clean runtime switch.

## Scope
- Introduce a render pipeline mode (`legacy`, `gather`, `validation` optional).
- Route frame rendering through one selected pipeline.
- Keep `legacy` as default mode.

## Suggested Touch Points
- `src/routeEditor/RouteEditorGLWidget.cpp`
- `src/tsre/Game.h`
- `src/tsre/Game.cpp`

## Requirements
- No manual source comment toggles required to switch pipelines.
- Add settings support:
  - `rendererPipeline = legacy|gather|validation` (case-insensitive)
  - invalid value fallback to `legacy`
  - `rendererPipelineHotSwap = true|false` (default `false`)
- Add runtime state variables in `Game`:
  - `requestedRendererPipeline`
  - `activeRendererPipeline` (can fallback to legacy on runtime failure)
- Optional debug hotkey: `Ctrl+Shift+F12` cycles `legacy -> gather -> validation -> legacy` when `rendererPipelineHotSwap = true`.
- `validation` mode definition:
  - render `legacy` to screen as authoritative output
  - run `gather` in parallel for parity/debug validation (offscreen and/or counters)
  - selection/editing behavior remains legacy-authoritative
- If gather pipeline init fails, auto-fallback to legacy and log reason.
- Shared frame setup code should be centralized to reduce behavior drift.

## Acceptance Criteria
- App starts with legacy rendering by default.
- Gather mode can be enabled without editing source comments.
- Validation mode can be enabled via settings and does not break editor interaction.
- Hotkey switch works only when `rendererPipelineHotSwap = true`.
- Invalid `rendererPipeline` setting safely falls back to `legacy`.
- Both modes compile and run.
- No regression in legacy mode behavior.

## Out Of Scope
- Implementing missing gather rendering features (handled by later tasks).
