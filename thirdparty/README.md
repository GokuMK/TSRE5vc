# Third-party code

Libraries TSRE builds from source, kept apart from TSRE's own code in
`src/` (user, 2026-10-25). Each folder holds the library, its licence and a
README with the version, where it came from (with a checksum) and any
changes TSRE made; to update one, follow its README.

| Folder | Library | Licence | Changed by TSRE |
|---|---|---|---|
| `earcut/` | earcut.hpp, polygon triangulation | ISC | no |
| `lua/` | Lua 5.5, scripting (compiled as C++) | MIT | no |
| `miniaudio/` | miniaudio, audio playback | public domain or MIT-0 | no |
| `miniz/` | miniz, deflate and ZIP | MIT | yes, listed in its README |
| `rmlui/` | RmlUi 6.3, the game UI (a subset) | MIT | no; one file replaced in the build |

`thirdparty/` is an include root: include them as `<earcut/earcut.hpp>`,
`<miniaudio/miniaudio.h>`, `<miniz/miniz.h>`; Lua's headers through
`src/tsre/script/LuaApi.h`, RmlUi's as `<RmlUi/Core.h>` (its `Include/` is
the root, through `tsre_rmlui`). Their warnings are not ours
to fix: it is a system include root (no warnings from these headers), and
what TSRE compiles of them on its own (miniaudio's implementation, in
`src/tsre/sound/Miniaudio.cpp`) builds with warnings off.
