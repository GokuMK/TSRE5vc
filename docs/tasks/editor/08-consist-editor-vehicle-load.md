# Task 08 - Consist Editor: loading the vehicles

Status: the vehicle files are read on worker threads, on
`feature/conedit-parallel-load` (2026-11-10). Three things are noted for later
(the user, 2026-11-10): folders left out of the scan, a vehicle index, and
loads chosen per car.

## The report (2026-11-10)

The user: from a cold start, loading TRAINS in the Consist Editor took so long
that the editor had to be killed.

**Cause.** Since [MaSzyna task 02](../maszyna/02-vehicles-as-eng.md#trainset-subfolders),
a `TRAINSET` folder with no vehicle files counts as a group, and its
subfolders are scanned for vehicles of every format (the user's rule).
`C:\trainsim\TRAINS\TRAINSET\Nowy folder` held 47 parked MMP freight packs:
18,691 WAGs, all in its subfolders.
- The editor read 23,030 vehicles instead of 4,339.
- The extra files were not in the disk cache, so the load ran for many
  minutes. The bench passed 300 s even with the rest warm.
- Before the change, neither the scan nor MSTS's ConBuilder listed them, and
  they could not be used where they were. A consist only reaches
  `TRAINSET\<folder>`, and their `..\MMP_COMMON` container shapes pointed
  inside `Nowy folder`, where there are none.
- The user moved the folder out of `TRAINSET`.

**The cold start was slow before the change too.** The 4,339 vehicles took
64.7 s from cold and 5.4 to 6.5 s warm. Each file was read one after another
on the main thread, and from cold most of that time is waiting for the disk.

## Built: the vehicle files read on workers

**`EngLib::loadAll`:**
- It lists the folders as before.
- Each folder is now listed once. Before, it was listed three or four times:
  for its vehicle files, again to list them, and twice for MaSzyna's check.
- MaSzyna's check runs only in a folder that holds an `.mmd`.

**`VehiclePreload::readAll`:**
- It reads the vehicles that are not loaded yet on a thread pool of its own,
  one thread per core.
- The main thread waits and updates the progress dialog every 50 ms. The
  editor has nothing else to do during the load.
- The shared pool of the timetables' read-ahead stays at 3 threads, because
  that pool runs beside a view.

**Making the cars:**
- They are made afterwards on the main thread, in the listed order, from the
  definitions read (`Eng(…, read)` and `Eng::load(read)`).
- A vehicle that is already loaded is counted again (`ref`), as before.
- `Refresh` and `Force reload` take the same path, so a refresh reads only
  the new files.

**Thread safety.** `VehicleDefinition::read` keeps its shared state under
locks: the include cache (`SimisDocument`), the shared Lights blocks, and
MaSzyna's catalogues, vehicles, materials and texts. The timetables'
read-ahead already reads on workers, MaSzyna rows included.

**Measured** with `vehicle-load` on the user's `C:\trainsim` (4,339 vehicles,
4,343 with the MaSzyna rows), Release, Steam Deck:

| | main | branch |
| --- | --- | --- |
| Cold | 70.5 s | 15.0 s |
| Warm | 6.0 to 6.1 s | 1.95 to 2.1 s |

**How "cold" was measured.** Emptying Windows' file cache needs
administrator rights, which the test shell does not have.
- Instead, the files the load reads were copied twice with `robocopy /J`
  into scratch game roots: 10,130 files, 69 MB, without shapes, textures or
  sounds. `/J` copies with unbuffered I/O, so the copies are not in the cache.
- Main read one copy and the branch the other, each for the first time. Both
  loaded the same 4,343 vehicles as from `C:\trainsim`.
- The copies are new files, so Windows Defender also scans each one on first
  open, as it would content just installed. A cold start of files Defender
  has seen before may wait less.
- Main's first runs after something pushed the real files out of the cache
  agree: 83.7 s after a build of 317 files, and 64.7 s after the editor was
  killed while reading 18,691 other files (that one before the MaSzyna
  merge).

**Checks:**
- `vehicle-load` now also reads every vehicle again, alone on the main
  thread. It compares that definition with the one read on the workers:
  names, type, brake system, shape, size, mass, wheels, couplings, 3D cab,
  files read, problems, and the Wagon and Engine blocks written out. All
  4,343 match, cold and warm.
- Build `all` and ctest pass (11 of 11).
- `e3d-file` (71, MaSzyna rows through `loadAll` on a scratch root),
  `vehicle-file` (41), `maszyna-vehicle` (90), `eng-lights` (43) and
  `vehicle-files` (30) pass.
- `timetable-read` passes its read-ahead checks. Its one failure, the letter
  case check, fails on main too: it needs a case-sensitive filesystem.

## For later (the user, 2026-11-10)

### 1. Folders left out of the scan

A setting listing folder names, or patterns, that the `TRAINSET` scan skips:
parked packs, backups, a stray "Nowy folder". Today the only way to hide a
folder is to move it out of `TRAINSET`.

### 2. A vehicle index

One file holding what the editor's list shows for each vehicle, with each
file's size and date.
- At start, the folders are listed (the listing gives the dates) and only
  new or changed files are read.
- From cold, that means one file read instead of thousands.

Open:
- Where it lives: TSRE's cache folder, not the content.
- Which fields the list needs: names, type, couplings, size, mass, shape.
- How a car gets its full definition when it is used (placed in a consist,
  driven, its cab shown), read on first use.

### 3. Loads chosen per car, and the list grouped

**Why packs hold thousands of WAGs.** In MSTS and Open Rails, a consist entry
names one WAG file and nothing else. A car cannot pick its load, number or
end marker, so every combination is its own file.
- MMP's `CH-HUPAC_Sggmrss_MMP` has 426 WAGs: 142 container loads × 3
  variants. The variants are the A half and the B half of the twin wagon,
  and the B half as the train's end ("B End DEU", shape `_B-ZS`).
- Two WAGs differ only in the Wagon name and the shown name. Each is about
  3 KB.
- The load is in a second copy in `OPENRAILS\`. It includes the WAG and adds
  one `FreightAnimStatic` container shape from `MMP_COMMON`.

**Open Rails' way, for containers** (since 1.5; the commit "Containers",
2022-05):
- A wagon in a consist takes `LoadData ( name folder position [state] )`, one
  per container (`Orts.Formats.Msts/ActivityFile.cs`, class `Wagon`).
- The container is a JSON file, `TRAINS/TRAINSET/<folder>/<name>.load-or`
  (`FreightAnimations.cs:359`). It gives the shape, the type, the offset and
  the masses.
- The wagon declares its loading area: `ORTSFreightAnims ( LoadingAreaLength
  ( … ) Offset ( … ) IntakePoint ( … Container ) )`.
- Positions: `Rear`, `CenterRear`, `Center`, `CenterFront`, `Front`, `Above`.
- States: `Empty`, `Loaded`, `Random`.
- With it, a pack needs one WAG per wagon variant, and the loads go in the
  consist. Such a consist runs in Open Rails; MSTS reads none of it.

**What Open Rails does not cover.**
- Its container types are ISO only (`ContainerType`: `C20ft`, `C40ft`,
  `C40ftHC`, `C45ft`, `C45ftHC`, `C48ft`, `C53ft`).
- So no swap bodies (MMP's 7.45 m and 13.6 m `sw` loads), no semitrailers
  on pocket wagons (the Sdggmrss T4 and T5 packs), and no other static
  freight.
- Those would need one of:
  - a TSRE extension in the same form, which Open Rails would show empty;
  - the MaSzyna way, rows named `<type>#<variant>`.

**TSRE today.** It keeps a consist's `LoadData` as a kept block
([task 07](07-activity-editing.md)). It does not draw or edit the loads.

**In the Consist Editor.** One row per wagon variant, with the load picked
per car. `CH-HUPAC_Sggmrss_MMP`'s 426 rows would become 3. This is also the
grouping of the list.
