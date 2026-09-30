# Open Rails game-root content path case review

Status: complete static review; no code changes made.

Baseline: Open Rails `master` at `6b2e724befb1693f3b6c1c73c1e84c2aeee9bff9`
(2026-09-23).

## Result

Open Rails does **not** currently use one case-consistent game-root layout. Windows
hides the problem, but a case-sensitive filesystem exposes real missing-file and
missing-directory failures.

The most serious example is the fixed route override directory. Main runtime code
requests the same directory as `OpenRails`, `Openrails`, and `openrails`. Other
runtime paths similarly mix `SHAPES`/`Shapes`/`shapes`, `SOUND`/`Sound`/`sound`,
`GLOBAL`/`Global`/`global`, and `TRAINS/TRAINSET`/`trains/trainset`.

There is useful existing structure: `MSTSPath` and most of `Orts.Menu` already use
uppercase structural directories and lowercase implicit suffixes. The inconsistent
callers largely bypass those helpers.

## Reference policy taken from TSRE5vc

The policy used for this review comes from:

- `TSRE5vc/docs/tasks/core/case-sensitive-filepaths.md`, especially A3 and B2.
- `TSRE5vc/docs/tasks/core/case-sensitive-filepaths-stage-a.md`.
- `TSRE5vc/docs/tasks/core/case-sensitive-filepaths-stage1.md`.
- `TSRE5vc/docs/tasks/core/case-sensitive-filepaths-stage3.md`.
- The trainsim and Conedit follow-up reviews in the same directory.

The agreed rules relevant to ORTS are:

| Kind | Rule |
| --- | --- |
| Fixed structural directories | Uppercase: `ROUTES`, `GLOBAL`, `TRAINS`, `SHAPES`, `TEXTURES`, `TRAINSET`, `CONSISTS`, `WORLD`, `TILES`, `LO_TILES`, `TERRTEX`, `TD`, `ACTIVITIES`, `SERVICES`, `TRAFFIC`, `PATHS`, `SOUND`, `ENVFILES`, `CABVIEW`. |
| Route and OR-specific structural directories | Uppercase where they have a fixed role, including `OPENRAILS`, `ADDONS`, `PROCEDURAL`, `TRACKPROFILES`, and `TERRAIN_MAPS`. Seasonal directories are also uppercase. |
| Dynamic directories | Preserve actual/authored spelling, including TRAINSET product folders and custom directories such as `common.snd`, `track_b`, and `OpenRailsCZSK`. |
| Fixed system basenames | Lowercase: `tsection.dat`, `forests.dat`, `sigcfg.dat`, `sigscr.dat`, `carspawn.dat`, `ssource.dat`, `ttype.dat`, `speedpost.dat`, `gantry.dat`, `hazards.dat`, `telepole.dat`, `terrainmaterials.dat`, `weathertransitions.dat`, `PROCEDURAL/shapetemplates.dat`, and `ENVFILES/editor.env`. |
| Explicit references | Preserve authored spelling. Do not lowercase an I/O path merely to make a key. |
| Implicit/generated filenames | Preserve the stem and use the conventional lowercase suffix, for example `.eng`, `.wag`, `.con`, `.srv`, `.pat`, `.sd`, and `.dds`. |
| Files found only by code enumeration | Clarified target policy: the converter should give discovery-only files the canonical lowercase spelling expected by code. At minimum, their suffix must be lowercase so patterns such as `*.trk` and `*.act` work on a case-sensitive filesystem. |
| Logical identity | It may be case-insensitive, but it must be separate from the physical path used for I/O. |

`OPENRAILS` deserves an explicit upstream decision. Open Rails historically spells
its overlay directory `OpenRails`, while the TSRE content policy classifies a
recognized override directory as fixed structural and therefore uppercase. Either
choice can work, but the applications must choose the same spelling. Open Rails'
current three-way spelling is invalid under either choice.

## Scope and method

This is a source-level audit of game-root and route content paths under `Source/`.
It covers direct literals, path composition, implicit suffix creation, directory
enumeration, and path-string classification in the simulator, menu, format
libraries, Content Checker, and contributed tools.

The audit intentionally excludes UI words, parser tokens, registry keys, program
resources, user-data paths, comments, and arbitrary filenames supplied explicitly
by content. No content was renamed and no ORTS source was edited. Runtime testing
on a case-sensitive filesystem remains necessary after implementation.

## Critical runtime findings

### 1. One `OPENRAILS` role has three spellings

`Source/Orts.Simulation/Simulation/Simulator.cs` alone uses:

- `OpenRails` at line 287 for route signal configuration;
- `Openrails` at lines 320-321 for the route `tsection.dat` override;
- `Activities/Openrails` at line 446 for station population data;
- `openrails` at lines 451, 489, and 553 for `turntables.dat`.

Related mismatches occur in:

- `Source/RunActivity/Viewer3D/Viewer.cs:343` (`OpenRails`);
- `Source/Orts.Formats.OR/MSTSData.cs:41,52-53` (`OpenRails` and `Openrails`);
- `Source/Orts.Common/ORFileHelper.cs:47` (`OpenRails` for per-file overrides);
- `Source/ContentChecker/TrackFileLoader.cs:106,128` (`OpenRails`);
- `Source/Contrib/TrackViewer/Drawing/DrawTrackDB.cs:75-76,96`;
- `Source/Contrib/TimetableEditor/MainWindow.xaml.cs:94-390`
  (`activities/openrails` repeatedly);
- `Source/Contrib/ContentManager/ContentMSTS.cs:125`.

Impact: a route cannot expose one physical override directory which all these
features find on a case-sensitive filesystem.

### 2. Fixed catalog basenames use the wrong case

Generated direct accesses use uppercase fixed filenames:

- `TSECTION.DAT` in `Simulator.cs:320-327`,
  `Orts.Formats.OR/MSTSData.cs:52-59`, and
  `Contrib/TrackViewer/Drawing/DrawTrackDB.cs:75-82`;
- `TTYPE.DAT` in `RunActivity/Viewer3D/Viewer.cs:356`.

The agreed names are `tsection.dat` and `ttype.dat`. Other nearby accesses already
use `sigcfg.dat`, `carspawn.dat`, and `speedpost.dat` correctly, demonstrating that
the source itself has no single basename convention.

### 3. Core simulator paths bypass the canonical structural spelling

Main simulation:

| Area | Noncanonical access | Representative locations |
| --- | --- | --- |
| Route shapes | `shapes`, `Shapes` | `Simulator.cs:345,353,360,451,489,553`; `Viewer.cs:384`; `Scenery.cs:334` |
| World | `World` | `RunActivity/Viewer3D/Scenery.cs:269` |
| Route/global sound | `Sound`, `sound` | `Viewer3D/World.cs:88`; `Signals.cs:278`; `Shapes.cs:834-1690`; `Viewer.cs:936`; `MSTSWagonViewer.cs:1467` |
| Global shapes/textures | `Global/Shapes`, `Global/Textures`, `global/textures` | `DynamicTrack.cs:149`; `Shapes.cs:933`; `Wire.cs:384-391` |
| Route textures | `Textures`, `TerrTex` | `Viewer3D/Common/Helpers.cs:85,90,95`; `Wire.cs:380` |
| Environment textures | `envfiles/textures` | `MSTSSky.cs:506,543-544`; `Water.cs:52` |
| Track profiles | `TrackProfiles` | `DynamicTrack.cs:349` |

Canonical structural spellings are `SHAPES`, `WORLD`, `SOUND`,
`GLOBAL/SHAPES`, `GLOBAL/TEXTURES`, `TEXTURES`, `TERRTEX`,
`ENVFILES/TEXTURES`, and `TRACKPROFILES`.

### 4. Rolling-stock lookup uses lowercase fixed directories

Direct I/O paths use `trains/trainset` in:

- `Simulator.cs:1312,1537`;
- `Simulation/AIs/AI.cs:868`;
- `Simulation/Container.cs:536,560`;
- `Simulation/RollingStocks/SubSystems/FreightAnimations.cs:318,359`;
- `MultiPlayer/OnlineTrains.cs:191`;
- `MultiPlayer/MPManager.cs:889,896,914`;
- `MultiPlayer/Message.cs:521,575,587,669,1361,1588,1658`.

The same areas use `trains/orts_eot` in `Simulator.cs:1318,1543`,
`AI.cs:875`, and `ProcessTimetable.cs:2467`, even though
`Simulator.cs:284` establishes `TRAINS/ORTS_EOT`.

`TRAINS` and `TRAINSET` are fixed uppercase directories. Product folders beneath
`TRAINSET` should continue to preserve `wagon.Folder` exactly.

### 5. Timetable and activity paths use title case

- `ProcessTimetable.cs:1157,1351,3027` uses `Paths`.
- `ProcessTimetable.cs:1362-1375` uses `Trains`, `Consists`, and `trainset`.
- `Orts.Formats.Msts/ActivityFile.cs:744` uses `Traffic`.
- `Simulator.cs:446` uses `Activities`.

These should use `PATHS`, `TRAINS`, `CONSISTS`, `TRAINSET`, `TRAFFIC`, and
`ACTIVITIES` for direct fixed-location access.

### 6. Generated suffixes are inconsistent

The simulator generates uppercase suffixes instead of the agreed lowercase
implicit suffixes:

- `Simulator.cs:411-412,428-429`: `PAT`, `CON` via `Path.ChangeExtension`;
- `Simulator.cs:802-804,1284-1285,1429,1439-1440`: `.SRV`, `.CON`, `.PAT`;
- `Simulation/AIs/AI.cs:802,821,823`: `.SRV`, `.CON`, `.PAT`;
- `Orts.Content/MSTSPath.cs:134`: `.TIT` (apparently unused in this tree).

This conflicts with the otherwise good lowercase `.srv`, `.con`, and `.pat`
generation in `MSTSPath` and `Orts.Menu`.

## Discovery and comparison findings

### Lowercase-only wildcard discovery: conditional on the converter contract

Calls such as `Directory.GetFiles(directory, "*.trk")` are case-insensitive on
Windows but normally case-sensitive on Linux. Under the clarified policy, this is
not an ORTS defect for a converted canonical tree: TSRE should rename files which
are found only by code enumeration to the lowercase spelling expected by these
patterns.

The current TSRE implementation has a **missed exception** to that contract:

- `case-sensitive-filepaths-stage-a.md:18-20` currently says an enumerated
  `.TRK` preserves its complete filename, including an uppercase suffix;
- `tests/contentCase/ContentCaseTests.cpp:252` deliberately creates
  `Route.TRK`;
- `ContentCase.cpp:771-793` retains the existing leaf by default and lowercases
  fixed catalogs, but has no general rule for discovery-only suffixes.

The intended correction is `Route.TRK` -> `Route.trk`: preserve the stem and
normalize the code-discovered suffix. The corresponding TSRE documentation and
test expectation should change with that fix.

Therefore the wildcard list below is primarily an inventory of filenames whose
lowercase spelling TSRE must guarantee. It becomes an ORTS compatibility issue
only if Open Rails is also expected to load arbitrary unconverted mixed-case
content.

Core affected discovery includes:

- `Orts.Content/MSTSPath.cs:100` (`*.trk`);
- `Orts.Menu/Activities.cs:114` (`*.act`);
- `Orts.Menu/Consists.cs:94` (`*.con`);
- `Orts.Menu/Paths.cs:95` (`*.pat`);
- `ContentChecker/TrackFileLoader.cs:122-181` (`*.act`, `*.t`, `*.pat`, `*.w`);
- `ContentChecker/TrackDataBaseLoader.cs:51` (`*.ws`);
- `Orts.Simulation/Simulation/Signalling/Signals.cs:415` (`*.w`);
- `Orts.Simulation/Simulation/RollingStocks/SubSystems/EOT.cs:55` (`*.eot`).

Contributed tools repeat the pattern for routes, activities, consists, paths,
world tiles, terrain tiles, pools, and turntables. For canonical converted
content, add these families to TSRE's lowercase discovery-file policy and tests.
As optional ORTS hardening for unconverted content, enumeration could instead
compare extensions with `OrdinalIgnoreCase` while retaining the exact discovered
filename for I/O.

### Path classification assumes lowercase backslash substrings

- `Viewer3D/Shapes.cs:2038,2040` tests `\\global\\` and `\\trainset\\`
  with case-sensitive `Contains`.
- `Viewer3D/Materials.cs:755` tests `\\trainset\\` case-sensitively.
- `Contrib/DataValidator/TerrainValidator.cs:36` tests `\\lo_tiles\\`
  case-sensitively.
- `MultiPlayer/MPManager.cs:902` finds `\\trains\\trainset\\` without an
  ignore-case comparison.

With canonical uppercase directories these tests fail even on Windows because
they inspect strings rather than asking the filesystem. Several multiplayer
uses correctly specify `StringComparison.OrdinalIgnoreCase`; those are not case
defects, although their separator assumptions remain non-portable.

`Orts.Formats.Msts/WorldFile.cs:43` uppercases before locating `\\WORLD\\W`, so
its casing comparison is safe. It still assumes Windows separators.

## Menu, checker, and contributed tools

The following are direct filesystem mismatches, not merely labels:

| Component | Current spellings |
| --- | --- |
| Main menu root detection | `Menu/ContentForm.cs:883,892` checks `routes`. |
| Content Checker | `Global`, `OpenRails`, `Activities`, `Tiles`, `Lo_Tiles`, `Paths`, `World`, and `shapes` in `TrackFileLoader.cs` and `CarSpawnLoader.cs`. |
| Content Manager | `Routes`, `Trains`, `Trainset`, `Consists`, `Activities`, `Paths`, `Services`, and `Traffic` in `Contrib/ContentManager/ContentMSTS.cs` and `Models/Consist.cs`. |
| Timetable Editor | `Trains/Consists`, `Paths`, and repeated `activities/openrails` in `Contrib/TimetableEditor`; exported ZIP entries also use `Routes/.../Paths`. |
| Activity Editor | `activities` in `LibAE/Formats/ActivityInfo.cs:166`; `routes` in `ActivityEditor/Preference/Options.cs:93`. |
| Data tools | `Tiles` in `DataCollector/Program.cs:89` and `DataConverter/TerrainConverter.cs:73`; lowercase substring test in `DataValidator/TerrainValidator.cs:36`. |
| Track Viewer | The main world/terrain paths are already uppercase, but route override `Openrails` and uppercase `TSECTION.DAT` remain in `Drawing/DrawTrackDB.cs:75-82`. |

## Already compliant or mostly compliant

- `Orts.Content/MSTSPath.cs` uses uppercase fixed directories for `ROUTES`,
  `TRAINS/CONSISTS`, `TRAINS/TRAINSET`, `SOUND`, `ACTIVITIES`, `SERVICES`,
  `TRAFFIC`, and `PATHS`; its normal generated suffixes are lowercase.
- `Orts.Menu/Routes.cs`, `Consists.cs`, `Activities.cs`, `Paths.cs`, and
  `ORTimetables.cs` use the expected uppercase structural directories.
- `ContentChecker/TrackDataBaseLoader.cs` uses `WORLD`.
- Several viewer paths already use `ENVFILES`, `TILES`, `LO_TILES`, `TERRTEX`,
  `GLOBAL/TEXTURES`, and `SOUND` correctly.
- Comparisons in `ContentChecker/SmsLoader.cs` lowercase directory basenames only
  for symbolic classification. They do not alter the physical I/O path and are
  therefore acceptable.
- Multiplayer `LastIndexOf` calls which explicitly use
  `StringComparison.OrdinalIgnoreCase` are acceptable as logical parsing, subject
  to the separate separator concern.

## Separate Linux portability issue: path separators

This review's primary subject is case, but the source also constructs many content
paths by concatenating literal backslashes. On a native Unix .NET runtime,
backslash is not the directory separator. Fixing case alone therefore does not
establish native Linux runtime compatibility.

The current application projects target `net6-windows`. Building them on Linux
is therefore not evidence that these content paths work during native Linux
execution; build portability and runtime filesystem portability are separate.

Examples include `ORFileHelper.cs:47`, most paths in `Simulator.cs`, and the
`\\WORLD\\W` parsing in `WorldFile.cs:43`. A case patch should avoid adding more
backslash concatenation and should use `Path.Combine`, `Path.GetRelativePath`, and
component-based comparisons. Separator conversion is best tracked as an explicit
companion task rather than hidden inside a nominal case-only patch.

## Recommended implementation order

1. Agree with Open Rails upstream on the fixed override spelling, especially
   `OPENRAILS` versus historical `OpenRails`, and document the complete layout.
2. Introduce one shared set of structural directory and fixed-basename constants
   in a low-level common/content assembly. Do not scatter replacements first.
3. Migrate the simulator and viewer critical paths: route initialization,
   `tsection.dat`, shapes, textures, sounds, world tiles, environment files, and
   rolling stock.
4. Migrate menu and timetable paths, then Content Checker and contributed tools.
5. In TSRE, define and test the lowercase contract for files found only by code
   enumeration. Treat case-insensitive ORTS enumeration as optional compatibility
   hardening rather than a requirement for canonical converted content.
6. Keep physical paths separate from folded cache/logical keys; use explicit
   `OrdinalIgnoreCase` only for format-defined logical comparisons.
7. Add tests using a temporary tree containing only canonical uppercase
   structural directories, lowercase fixed files, mixed-case dynamic product
   directories, and explicit uppercase source suffixes.
8. Run a second test tree with deliberately wrong spellings and assert clear
   missing-path diagnostics rather than silent case searching.

## Suggested patch boundaries

This should not be one very large mechanical case-replacement patch. A reviewable
series would be:

1. layout contract/constants plus case-sensitive temporary-tree tests;
2. route startup and fixed catalogs;
3. viewer shapes/textures/sounds/world/environment;
4. trainset, timetable, AI, and multiplayer paths;
5. menu, Content Checker, and contributed tools;
6. optional mixed-case legacy-content discovery hardening;
7. separator portability where required for native Linux execution.

The first implementation patch should also contain an inventory assertion or
focused source test so that `OpenRails`/`Openrails`/`openrails` cannot diverge
again.
