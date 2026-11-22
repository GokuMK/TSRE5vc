# Task 11 - Find a model by its .s file name in the object palette

Status 2026-11-22: code, strings and suite written; not built or run here
(this machine has no Qt, CMake or compiler on PATH). Asked for by the user
(2026-11-22): "支持在线路编辑器中，根据模型文件.s名称查找模型并选择应用" - find a
model in the route editor by its model file (.s) name and select/apply it.

## Goal
- The palette search box finds an entry by the name of its model file
  (`RefItem::filename`, a `.s` shape), not only by the description the list
  shows, and the result can be applied without hunting through the class
  drop-downs.
- The old description search keeps working.

## Search and rank
`ObjTools::refItemSearchRank` (public static, `src/routeEditor/ObjTools.cpp:724`)
compares the text with each of the entry's model file names and returns:
- `0`: a name equals the text, with or without `.s`, any case, with either
  path separator. Only the basename is compared, so `GP38.s`,
  `models/GP38.s` and `C:/TSRE/models/GP38.s` all match `gp38`.
- `1`: a name contains the text, or its basename, or its stem.
- `2`: only the description contains the text; the empty text lists all.
- `-1`: no match.
`refSearchSelected` (`src/routeEditor/ObjTools.cpp:749`) collects the hits
from every class, sorts them by rank and then by the visible text, and
fills the list; the visible text falls back to the model file name when the
description is empty (`refItemSearchText`). Each row's tooltip lists the
entry's model files.

## Applying
Enter in the search box (`refSearchActivated`) or a double click on a row
(`refListActivated`) selects the entry, stores it as `route->ref->selected`
(the normal `refListSelected` path) and emits `enableTool("placeTool")`,
which is the Place New button's path; the next click in the view places the
model. `src/routeEditor/ObjTools.cpp:790`, `src/routeEditor/ObjTools.cpp:795`.

## Tests and strings
- New suite `obj-search` (`src/tsre/tests/ObjSearchTestSuite.h`, `.cpp`, 16
  checks) covers exact name with and without `.s`, case and surrounding
  spaces, both path separators, relative and full paths, partial names, a
  second file name, description-only entries, no match, empty text and the
  rank precedence. Registered in `src/tsre/tests/TestRunner.cpp` (include,
  `listSuites`, dispatch).
- New string `route.editor.obj.tools.tooltip.search.by.model.file` in
  `translations/tsre_en.ts`, `tsre_pl.ts` and `tsre_zh_CN.ts`;
  `scripts/validate_translations.py` passes (3814 IDs; English and
  Simplified Chinese complete, Polish placeholders consistent).
- Feature doc updated: `docs/features/ref-files-and-addons.md` section 5.
- Not done here: build, run the `obj-search` suite, live check in the
  editor (search by a known `.s` name, Enter, place).

## Notes
- Search text is not written into `Ref`; this is a palette-only search.
- The list types (`QListWidgetItem` type) still index `currentItemList`, so
  `refListSelected` is unchanged.
- Pseudo-classes (`#TDB#`, `#TSRE#`) are searched too because their entries
  live in `refItems`; their track/road shapes have the model file name in
  the description already.
