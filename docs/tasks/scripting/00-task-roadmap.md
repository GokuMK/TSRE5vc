# Scripting Task Roadmap

This folder holds the tasks for Lua in TSRE and for RmlUi, the HTML/CSS-like
UI library scripted with Lua. Both are put in once and shared: the game UI,
cab displays, loco scripts and advanced signal scripts (the driving
roadmap's decisions of 2026-10-17 and 2026-10-24) all build on them.

## Execution Order
- [ ] `01-lua-and-rmlui-foundations.md` (on `feature/scripting`: steps 1
  and 2, the Lua runtime and RmlUi, done 2026-10-25; next drawing through
  the render queue): Lua 5.5 and
  RmlUi 6.3 vendored, no other new dependency (RmlUi's fonts through Qt,
  not FreeType); the Lua runtime (`src/tsre/script/`): one state per
  thread, a sandbox, time and memory limits, state tables saved and
  restored; RmlUi in TSRE (`src/tsre/ui/`): drawn through the render queue
  by both renderers, Qt input, Lua in documents, data from the simulation's
  frame; first use: the play mode HUD as an RmlUi document.

## Later (other tasks, to be laid out)
- Cab displays: an RmlUi context drawn into a texture on a 3D cab's screen
  part or a 2D cab (needs offscreen targets in the render queue).
- Loco scripts on the simulation's Lua state (driving tasks, after the
  vehicle definitions).
- Advanced signal scripts in Lua (MSTS `sigscr` stays on its own
  interpreter).
- Game menus and dialogs in RmlUi; RmlUi's clip masks, filters and
  gradients when a document needs them.
