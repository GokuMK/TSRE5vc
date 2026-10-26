# RmlUi

HTML/CSS-like user interface library with a Lua plugin, used by TSRE's
game UI (`src/tsre/ui`, task scripting 01).

- Version: 6.3 (released 2026-08-22)
- Source: https://github.com/mikke89/RmlUi, tag `6.3`
  (`https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz`, SHA-256
  `d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede`)
- Licence: MIT, see `LICENSE.txt`; the bundled containers (robin_hood,
  itlib) are MIT too (`Include/RmlUi/Core/Containers/LICENSE.txt`); the
  debugger embeds the font Courier Prime Code, SIL OFL 1.1
  (`Source/Debugger/LICENSE.txt`). The About window shows all three.

Files: the release's `Include/`, `Source/Core`, `Source/Lua` and
`Source/Debugger` unchanged, without RmlUi's CMake and clang-format files.
Left out: the samples, tests and backends (TSRE has its own), the SVG and
Lottie plugins (they need lunasvg and rlottie), RmlUi's CMake build.

TSRE builds it as the static library `tsre_rmlui` (`CMakeLists.txt`) with
Lua compiled as C++ (`RMLUI_LUA_AS_CXX`) and RmlUi's own font engine, whose
one FreeType file (`Source/Core/FontEngineDefault/FreeTypeInterface.cpp`)
is left out of the build and replaced by TSRE's on Qt's `QRawFont`
(`src/tsre/ui/RmlQtFontInterface.cpp`): no FreeType library. To update,
replace these folders with a new release's (the same files left out),
check that `FreeTypeInterface.h` still declares the same functions, and
update this file.
