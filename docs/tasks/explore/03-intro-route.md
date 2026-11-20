# Explore task 03 - The intro route behind the menu (brief)

Status 2026-11-20: brief only, to be designed when tasks 01 and 02 are
done. Part of the [explore set](00-task-roadmap.md).

## Goal
The main menu over a living route instead of black, as Kingdom Come does:
the camera moving slowly over a place, trains passing.

## What it needs
- **The content:** `assets/root`, a small game root of TSRE's own
  (`GLOBAL`, `ROUTES/intro1`, the trains its scene uses).
  - How it ships is open (question 10: later). The user is thinking of one
    intro as a compressed archive in the appdata folder, as the places
    are: `appdata/0.7/geo/geo_cities_presets.zip` is tracked and unpacked
    with miniz into `assets/geo` the first time it is needed
    (`GeoPresetData.cpp`). The intro's would unpack into `assets/root`.
    The other way: a download, as `procedural_examples`.
  - Without it, the menu stays over black.
- **Another game root.** The intro's root is not the player's. Entering a
  run is then a route switch and a root switch. Editor task 10 left the
  root switch out. It needs an inventory like task 10's:
  - `Game::root` is read in 52 files of `src/tsre`;
  - read per route already: `tsection.dat` (a new `TSectionDAT` per
    route), the route's own caches;
  - by game root: consists and engines (`ConLib`, `EngLib`), sounds, the
    signal configuration, `GLOBAL/TRACKPROFILES`;
  - by absolute path, so fine: shapes and textures.
  - Back to the menu switches back.
- **The scene:**
  - the intro route's own activity or timetable running, AI trains only;
  - the camera moving along a few set shots (a place and a slow turn,
    written as `core.startup.camera` is).
  - Where the shots are kept is open. The rule against side files holds:
    they go in the route's own files (its TRK, or the activity), not in a
    new file beside them.
- **When it loads:** the menu shows at once over black. The intro loads
  behind it, but a route loads on the GUI thread, so the menu stands still
  meanwhile. Either load first behind a black screen with the logo, or
  wait for loading off the GUI thread (roadmap, later).
- **Memory:** the route switch keeps the old route (task 10: about 20 MB a
  switch on MINI and EUROPE1). Intro, run, menu, run grows every time.
  Freeing the old route is task 10's later step.
