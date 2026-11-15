# MaSzyna cab windows: which control opens which window (2026-11-16)

A review of all MaSzyna vehicle content for the next sound task: wiring a
MaSzyna cab's window glass to TSRE's window state (`windowLeft`,
`windowRight`), so an open window lets the outside in. MaSzyna gives the glass
no meaning; on the ET22 it is just `universal3:` and `universal4:` moving a
part. This report is the evidence for choosing a rule. Done by a subagent
with throwaway Python readers of `.mmd` and `.e3d` (ports of TSRE's
`MaszynaText`, `MaszynaCab::read` and `E3dFile`); nothing in TSRE or the
content was changed.

## Summary

- 1461 `.mmd` read, none failed; 2203 cab ends, 845 `.e3d` models read; the
  content has no `.t3d`. Left after dropping AI/static ends without a model
  and passenger-car interior views: **1006 driving cab ends in 666 `.mmd`,
  406 distinct** (same cab model, end and universal list).
- **214 of the 406 have window controls, 324 controls, every one a
  `universalN`.** No other label moves a window.
- Motion: 183 drop windows (down 0.46-0.71 m), 9 drop windows written
  inverted (value 0 = open: 4e grey, ep08), 98 hinged windows turned 90°
  with `wip` (elf, impuls, en57al), 28 sliding along the car, 6 st44 vent
  panes turned 72°. All toggles; 308 have `soundinc`/`sounddec`, 16 none.
- **Left or right only from the geometry** (x mirrored for cab 2). Names
  that claim a side are wrong 8 times in 214 (EP03 swapped; wmb10's cab 2
  names follow the vehicle). On the EP03 the pivots are 10 m from the glass.
- **No numbering convention:** `universal1` is the right window 99 times,
  the left 18. Within a vehicle both cabs agree.
- **MaSzyna gives universals no meaning:** `generictoggleN` only flips the
  gauge between 0 and 1, which moves the part and plays its sounds
  (`Train.cpp:6640-6680`). `CabWindowOpen` is the head-out mirror view
  (`drivermode.cpp:691-703`, `sound.cpp:963`). In Python screens
  `universal3` is replaced by the instrument light (`Train.cpp:777-783`).
- Near the windows but not windows: 54 side doors (303e/4e/ep08 `drzwi`,
  opposite the window), Vectron window latches, a button, a book cover and
  an ashtray lid on the side walls. 110 cab ends have side glass but no
  window control (EN57 family, SM42 and others).

## Candidate rules

Each takes the side from the geometry. A cab counts as right only if its
whole mapping matches the hand-made truth.

| Rule | 214 with windows (right / wrong / undecided) | 192 without |
|---|---|---|
| R0: a moving universal at 0.8 of half width or more | 146 / 68 / 0 | 188 / 4 / 0 |
| R0b: R0, its part tree under 1.2 m tall | 192 / 22 / 0 | 190 / 2 / 0 |
| R1: window word in the submodel name or a part under it | 100 / 94 / 20 | 188 / 0 / 4 |
| R1b: window word in the submodel name only | 126 / 88 / 0 | 188 / 0 / 4 |
| R2: window word in a sound file name | 204 / 10 / 0 | 192 / 0 / 0 |
| R3: moving glass at 0.8 of half width or more, under 1.2 m tall | 214 / 0 / 0 | 192 / 0 / 0 |
| R4: R1 or R2, and moving, at the side wall, under 1.2 m tall | 214 / 0 / 0 | 192 / 0 / 0 |

Where they go wrong:
- R0: side doors, Vectron latches, `wylacznik_universal_lol`, the `okladka`
  book, the ashtray lid, the en57akl/akm cabinet doors.
- R0b: latches (8 cabs), `wylacznik_universal_lol` (12), `okladka` (2),
  ashtray lid (2).
- R1/R1b: miss `box09` (73 controls), `plane19` (10), `plane86` (3); R1 also
  takes doors whose part trees contain glass names.
- R2: st44 (6 cabs) and ep03 (2) have no sounds; the 181's sun blind
  `roleta_nn` plays window sounds (`cd/181_v2/18x_cz_interior.mmd:195`).
- R3, R4: none, but their thresholds were picked looking at this content, so
  their score is a fit, not a test. Margins: windows at 0.91 of half width or
  more, the et22 back doors at 0.79, the latches at 0.94; windows at most
  0.90 m tall, doors at least 1.6 m. Half width from the narrower side of the
  model's box (the sm40 model has a part sticking out). R3 depends on telling
  glass (texture name or translucency).

**How far open:** `mov` |scale| m along the part's own Z (0.10-0.71 m),
`rot`/`wip` |scale|×360° (72° or 90°). Open = the value, except where a
vertical pane is higher at value 1 (where `soundinc` is the close sound):
there 1 − value (9 controls). For sliding, hinged and vent windows "value 0
= shut" is assumed; the 120 with sounds all have `soundinc` = open. Two
controls on one side (st44 window and vent, wmb10 two panes): the larger.

**Decisions for the user:**
- Do side doors count as an open side?
- Do cabs with side glass but no window control keep TSRE's own Ctrl+Q?
- Should the inverted 4e grey/ep08 windows start open, as they do in MaSzyna
  (gauges start at 0)?

## Evidence

### How it was read

- `.mmd` bytes decoded as CP1250 (no file skipped); `include … end` with
  `(pN)`, `[ a b ]` taking the first; cab<N>definition sections, the 386
  known labels, `cab<N>model:` clearing earlier controls.
- Cab models as TSRE names them (`.e3d` for a written `.t3d`); a control
  binds as `TGauge::Load` binds it, its submodel in any case, else
  `<name>_off`, in the cab model first, then the low-poly interior
  (`Gauge.cpp:192`).
- A part's place: the centre of the box around its geometry and every part
  under it, in the model frame (the root's `x'=-x, y'=z, z'=y`). Not the
  pivot.
- Left and right: +x is cab 1's left looking toward +z; cab 2 looks toward
  −z. MaSzyna puts cab 1's left mirror at +x (`TTrain::MirrorPosition`,
  `Train.cpp:10672-10678`; `vLeft`, `DynObj.cpp:2656, 2672`). Cross-check:
  360 of 366 cabs with an off-centre seat have `driver<N>sitpos` on the same
  side as `mainctrl`/`brakectrl` (the 6 others are SM42-family centre cabs).
- Truth: all 250 distinct universal groups classified by hand (window, side
  door, latch, other) from the part tree, textures, translucency, place,
  motion and sounds; then every moving universal at the side walls and every
  one with glass under it not called a window was checked again.

### What was not read

- 715 ends without `cab<N>model:` (AI/static), 42 `none`, 13 missing:
  `(p1)`/`(p2)` names (`en57akm_2013_v1/cfg/akm_2013_ra|rb`,
  `impuls_v1/36wea-a|c`), `cab<N>model: soundproofing:` (`elf_v1/27web-c|d`),
  absent files (`cd/bdmteeo_v2/g76_mod_294`, `gantry/24m/suwnica1`,
  `sa134_v1/214mb`, `road/berliet/pr110`, `road/crafter_mk2/crafter`).
- 427 ends with a model and no controls: passenger-car interior views
  written as cab ends.
- Also seen: 7 ends without `driver<N>pos` (et40 b-ends, et41 `203e-b*`,
  el16); `en57akm_2013_v1/cfg/*.inc` looked for in `cfg/`; 154 ends write a
  word where `i-doors:` expects a number; ED72's `korytarz.e3d` ends have
  `universal1: okno_kabiny` binding to nothing.

### The window controls

All 324 toggles, plain `mov`/`rot`/`wip` (never `movvar`/`rotvar`), friction
0.1-0.5 s (0 on the EP03), none with `soundproofing:`.

| Pattern | Controls | Families | Travel | Examples (`.mmd`, label, submodel, side) |
|---|---|---|---|---|
| A. Drop window, down at value 1 | 183 | 303e, 4e, ep07, et22, 181, vectron, eu04, ep03, ep05, eu05, et40, et41, e189, su46, sm40, sm41, ed72 | 0.46-0.71 m | `pkp/et22_v3/201e.mmd` universal3 `szyba_mechu` R, universal4 `plane19` L; `pkp/303e_v1/303e-409.mmd` universal1 `box09` R; `pkp/vectron_v1/vectron.mmd.inc:269,271` universal2 `okno_lewe` L, universal4 `okno_prawe` R |
| A'. Drop window inverted (value 0 = open) | 9 (32 `.mmd`) | 4e grey, ep08 | 0.53 m | `pkp/4e_v1/4e-ep-zez_grey.mmd:248` universal1 `box09` R (`soundinc: …window_close.wav`) |
| B. Hinged, `wip` 90° | 98 | elf, impuls, en57al | 90° | `pkp/elf_v1/21we-a.mmd:294-295` universal2 `okno_r` R, universal3 `okno_l` L |
| C. Sliding along the car | 28 | 21xm, sa134, st44, et42, wmb10 | 0.30-0.50 m (wmb10 0.10) | `pkp/21xm_v1/214m.mmd` universal2 `okienko_przesuwne_r` R; `pkp/st44_v1/st44-n.mmd:170` universal1 `OKNO_MECHANIK` R |
| D. Vent pane, 72° | 6 | st44 | 72° | `pkp/st44_v1/st44-n.mmd:168` universal2 `okno_odchyl` R |

- 255 move a frame or handle with the glass under it, 69 move the glass
  itself; all 324 have a glass texture under the moving part, 313 also draw
  translucent.
- Places: centres at 0.91-1.00 of half width; part trees 0.44-0.90 m tall;
  215 right, 109 left. Per model and end (210): both sides 100, right only
  103 (303e/4e/ep08 with the door on the left, ep05, eu05, et40, et41, et42,
  su46, st44, sm40, ed72), left only 7. One side only: on the driver's seat
  side (99 right; sm41's centre cab left).
- Names: `okno*`/`okienko*` 184, `szyba*` 46, generic 86 (`box09` 73,
  `plane19` 10, `plane86` 3), `uchwyt_*` 8. Sounds: `20786_201e_window_*`
  165, `20786_36wea_window_*` 68, `window_pcv_*` 46, `vectron_okno_*` 16,
  `23180_105na_window*` 13, none 16 (st44, ep03).

### Not windows, at the side walls

| What | × half width | Height | Cab ends |
|---|---|---|---|
| `drzwi`, side door 72° (303e/4e/ep08) | 0.94-0.96 | 1.6-1.7 m | 54 |
| `klamka_lewa/prawa`, window latch (vectron) | 0.94 | 0.18 m | 16 |
| `ramie_drzwi_l/r`, side door (wmb10) | 0.93-0.94 | 1.73 m | 4 |
| `wylacznik_universal_lol`, button (303e-1xxx) | 0.85-0.87 | 0.23 m | 12 |
| `okladka`, book cover (en57al/km) | 0.74-0.84 | — | 8 |
| `pokrywka_popielniczki`, ashtray lid (wmb10) | 0.95 | — | 2 |
| `lewe/prawe_drzwi`, cabinet doors (en57akl/akm) | 0.89-0.97 | 1.7-1.9 m | 4 |
| et22 back-wall doors | 0.78-0.79 | 1.8 m | 28 |

Sun blinds (`roleta*`, `oslona*`, `zaslonka*`) sit below 0.8 of half width.

### Glass no control moves

Side-wall glass sheets (lower bounds; glass merged into bigger meshes is
missed): in cabs with window controls, 49 left and 7 right sides have glass
without a control (ep05/eu05/et40 `szyba01`, 4e `szyba_bok01/03`, st44
`OKNO01`, su46 `szyby_okna`, impuls `szyba_l`, elf `okno_r-szyba`). Cabs
without any control: 110 with glass on both sides (en57 families, sm42/sp42/
su42, ep09, ew58, m62_mod, et22-2000, sn61/sr61, st43), 16 one side, 66
none found.

### Surprises

1. 4e grey and ep08 write the drop window inverted; gauges start at 0, so
   these windows start open in MaSzyna (from the code and geometry, not run).
2. EP03: window names swapped; both pivots at z −4.35, the glass at +5.75.
3. wmb10 uses one physical cab for both ends; its cab 2 names give the
   vehicle's sides.
4. 303e/4e/ep08: the left-side opening is a door.
5. Vectron window latches are separate universals inside the pane's tree.
6. The 181's sun blind plays window sounds.
7. `en57km_v1/en71km_ra|rb.mmd:202/206` binds `distcounter: bananek dgt` to
   the cab model's root: a rule must look only at the universal's own part
   tree.

## Conclusions (not tried in TSRE)

- Side and end from the geometry: the box centre of the control's part tree,
  x mirrored for cab 2, checked against the cab box's z range. Not names,
  not pivots.
- Pick the controls with R3 or R4 (both fit all 406); R2 alone gets 396.
  Position alone is not enough. Content from other authors may name parts
  less consistently, which favours R3.
- Opening: the value, or 1 − value where a vertical pane is higher at 1;
  the larger of two controls on a side.
