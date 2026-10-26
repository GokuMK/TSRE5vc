#pragma once

namespace TsreTests {
// RmlUi in TSRE (task scripting 01), without a GPU: a document laid out and
// drawn into a recording render interface; text in Noto Sans through Qt's
// fonts (widths, sizes, Polish letters, the glyph atlas); a TSRE translation
// id shown translated; a Lua handler run by a click in the UI's sandboxed
// state; an endless handler stopped by its deadline; a data binding
// following its value; compiled Lua refused; the debugger shown.
int runRmlUiSuite(bool verbose);
}
