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
| `ozz-animation/` | ozz-animation 0.16.0, skeletal animation (base, runtime, offline builders) | MIT | no |
| `recastnavigation/` | recastnavigation 1.6.0, walk areas and paths (Recast, Detour, DetourCrowd) | zlib | no |
| `rmlui/` | RmlUi 6.3, the game UI (a subset) | MIT | no; one file replaced in the build |
| `stb_vorbis/` | stb_vorbis 1.22, Ogg Vorbis decoding | MIT or public domain | no |

`thirdparty/` is an include root: include them as `<earcut/earcut.hpp>`,
`<miniaudio/miniaudio.h>`, `<miniz/miniz.h>`, stb_vorbis through
`src/tsre/sound/StbVorbisConfig.h`; Lua's headers through
`src/tsre/script/LuaApi.h`, RmlUi's as `<RmlUi/Core.h>` (its `Include/` is
the root, through `tsre_rmlui`), ozz's as `<ozz/animation/runtime/skeleton.h>`
(its `include/` is the root, through `tsre_ozz`), recastnavigation's as
`<Recast.h>`, `<DetourNavMesh.h>`, `<DetourCrowd.h>` (through
`tsre_recast`). Their warnings are not ours
to fix: it is a system include root (no warnings from these headers), and
what TSRE compiles of them on its own (miniaudio's and stb_vorbis's
implementations, in `src/tsre/sound/Miniaudio.cpp` and `StbVorbis.cpp`)
builds with warnings off.
