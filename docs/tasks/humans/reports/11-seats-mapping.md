# Task humans 11 - Seats and doors mapping (report)

Every car of OR_PSK_NEW's trainset and of MaSzyna's `dynamic/` scanned with
`extra/car-seats/car_seats.py`, every plan with seats found looked at, and the
`.sd` files of the cars it gets right (or right after `drop`) staged for the
user to apply. Brief: [11-seats-mapping.md](../11-seats-mapping.md). Made
2026-11-13 on branch `humans/seats-mapping` (from `feature/humans` 725f4b0b),
TSRE built Release (`-O3`, Ninja) in `../TSRE5vc-seats-map/build`. Nothing in
`~/ORTS` was written (checked against a marker file after staging), the tool
was not changed.

## In short
- **OR_PSK_NEW**: 5 221 vehicle files, 3 181 models (scan 7 min 14 s). 363
  models have 8 or more seats; outside `PKP_EN57-OR` and `PKP_EN57A-OR`
  (written before) they come as 140 layouts of 262 models. Verdicts: 69
  layouts right (124 models), 55 drop (112), 8 wrong (14), 8 none (12:
  service cars, freight loads).
- **MaSzyna**: 1 461 `.mmd`, 1 471 models (scan 13 min 36 s). 212 models with
  8 or more seats; outside `pkp/en57_v2` 125 layouts of 145 interiors: 107
  right (126), 10 drop (11), 6 wrong (6), 2 none (2).
- **Staged**: OR 381 `.sd` (344 of them new): 236 cars with seats (183 of
  them with doors), 145 with doors only; MaSzyna 137 interiors with seats
  (137 new `.sd`, no doors). Archives in `~/humans-11/` (below).
- **The finder's mistakes** come in 14 kinds (below, a list to pick from).
  The ones that cost most: back-to-back benches taken as one bench (ED72,
  120A, EN57AKŁ-type, MaSzyna 120A and SN61), crew seats in cabs (11
  families), three places on two first-class armchairs, two floor levels (the
  cab cars of ED78 and Impuls).
- **Already written, found wrong**: the 30 older-model EN57 driving cars in
  `PKP_EN57-OR` (`*ra.s`, `*rb.s` of the older model: PKP_EN57-1192, -1310,
  -1326, -1575, -1797, -1827, -907, -945, KMKOL_EN57-1755, PKPPR_EN57-687,
  PLKW_EN57-1805, PREG_EN57-1192, -1797, -686, SKMW_EN57-1636) have the cab's
  crew bench as seats: `ra` 57-58, `rb` 1-2 (plan:
  [or-en57-ra-cab-bench.png](11-plans/or-en57-ra-cab-bench.png), the same shape
  as `PKP_EN57/PKP_EN57-1827ra.s`). Not changed here (the brief: not written
  again); `drop <model> 57 58` (ra) and `drop <model> 1 2` (rb) fix them.

## How it was done

### Commands
In `../TSRE5vc-seats-map` (the tool run as it is, `python3 -I`):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8 --target TSRE5vc
python3 -I extra/car-seats/car_seats.py scan ~/ORTS/OR_PSK_NEW/TRAINS/TRAINSET \
    --tsre build/TSRE5vc --out ~/humans-11/scan-or                      # 7 min 14 s
python3 -I extra/car-seats/car_seats.py scan ~/ORTS/maszyna/dynamic --maszyna \
    --tsre build/TSRE5vc --out ~/humans-11/scan-maszyna                 # 13 min 36 s
```

12 cores, the default `--jobs`. Staging (every command with its output in
`~/humans-11/stage-or.log` and `stage-maszyna.log`):

```
car_seats.py write <the right and drop models> --root ~/ORTS/OR_PSK_NEW \
    --stage ~/humans-11/stage-or --archive ~/humans-11/steps-or/write      # 236 models, 397 s
car_seats.py doors <the doors-only models> --root ~/ORTS/OR_PSK_NEW \
    --stage ~/humans-11/stage-or --archive ~/humans-11/steps-or/doors      # 145 models, 195 s
car_seats.py drop <model> <numbers> --root ~/ORTS/OR_PSK_NEW \
    --stage ~/humans-11/stage-or --archive ~/humans-11/steps-or/drop-NNN   # one per model, 112 runs
car_seats.py write <the right and drop interiors> --root ~/ORTS/maszyna \
    --stage ~/humans-11/stage-maszyna --archive ~/humans-11/steps-maszyna/write  # 137 models, 63 s
car_seats.py drop ...                                                     # 11 runs
car_seats.py plan <the staged models> --sd --root <root> --stage <stage> \
    --out ~/humans-11/plans-staged/<set>/<folder>                         # one run a folder
```

`write`'s archive is made before the drops, so its zip holds the staged files
as they were before them, and each `drop` writes its own pair of zips. The
set's archives were therefore made at the end from the staging folders, with
the tool's own `archived()` (imported, unchanged): the zip of every staged
`.sd` by its path under the root, and the backup of the content's `.sd` at
the same paths. A note for the tool: `drop --archive <the set's name>`
replaces the set's zip with one file; an `archive` command, or `drop`
updating the set's zip, would save the step.

### Reading the plans
- **Layouts**: models whose finder results (seats, aisle, passages, doors)
  agree to 2 cm share a layout (repaints, the same shape in two folders):
  `L001`-`L140` (OR) and `M001`-`M125` (MaSzyna), most used family first. One
  plan of each layout was looked at; four to a sheet at full scale (OR) or
  scaled down where a family repeats itself (MaSzyna's EN57, Impuls, EN57AL),
  doubtful ones zoomed in. The numbers below are the plans' (and `write`'s).
- **A review aid** (a script beside the scan, not the finder): each seat's
  cushion depth along the way its sitter faces, and the backrest faces through
  it and in front of it. A seat on a cushion deeper than 0.8 m with a backrest
  through it is two benches back to back taken as one; it pointed at the
  plans to zoom in. It is not a verdict: back-to-back benches the finder split
  right (EN57-1291, MAV Bpmz, 810) measure deep too.
- **Doors**: measured for every staged OR model with door parts (off the body
  side by more than 0.25 m, wider than 2.2 m, narrower than 0.55 m, sides
  with different counts) and looked at on one plan a family.
- **Passenger cars**: a vehicle file typed `Carriage`, with
  `PassengerCapacity`, or a multiple unit by its folder's name (EN57, ED72,
  SA1xx...); MaSzyna: a passengers load or door places in the `.mmd`.
- **Train uses** (OR): PSK_2026's `#consist` row, 338 trains, each consist's
  vehicles, each vehicle's models; a train counts once per vehicle file it
  uses (an upper bound for models used by two vehicle files of one train).

## OR_PSK_NEW

### Cars with seats found
Most used first (train uses in PSK_2026, then vehicle files). Rows of right
layouts of one family are joined. `drop` numbers are seats, `d` numbers
doors.

| Layout | Model (or family) | Vehicle files | Train uses | Seats | Verdict | Notes |
|---|---|---|---|---|---|---|
| L026-L027 | `PKOR_Elf1/` (9 models: `KSL_EN76-001B.s`, `KSL_EN76-001C.s`, `KSL_EN76-004B.s` +6) | 9 | 54 | 16-30 | right | Elf B (middle): one-direction rows; the aisle line 0.23 m off the gap's middle (the raised floors over the bogies taken as benches); Elf D (end): 16 seats between the vestibules; cab and folding area beyond without seats |
| L044 | `PKOR_ED160/` `PKPIC_ED160-013a.s` | 1 | 6 | 64 | drop 41-44 | ED160 a: 41-44 on the stairs up to the raised floor |
| L045 | `PKOR_ED160/` `PKPIC_ED160-013b.s` | 1 | 6 | 49 | wrong | ED160 b (1st class 2+1): the 2+1 sides swap at the vestibule, the one straight aisle runs through the right saloon's armchairs (37, 41, 45 on it); three places on each pair of armchairs; 49 the driver's seat |
| L046 | `PKOR_ED160/` `PKPIC_ED160-013c.s` | 1 | 6 | 30 | drop 2,6,10,14,18,22,26 | ED160 c (1st class 2+1): the middle of three places on each pair of armchairs (on the armrests) |
| L047-L051 | `PKOR_ED160/` (5 models: `PKPIC_ED160-013d.s`, `PKPIC_ED160-013e.s`, `PKPIC_ED160-013f.s` +2) | 5 | 30 | 8-60 | right | ED160 d (bistro car): 8 seats at one end, bar area without; ED160 e: 2+2, stairs without seats; ED160 f; ED160 g; ED160 h |
| L023-L025 | `PKP_EN57SPOT-OR/` (13 models: `PREG_EN57-2076ra.s`, `PREG_EN57-2023ra.s`, `PREG_EN57-2072ra.s` +10) | 15 | 30 | 41-76 | right | EN57SPOT ra (refurbished EN57): no cab seat found; doors D1-D4; EN57SPOT rb: open multi-purpose middle without seats, as modelled; EN57SPOT s |
| L036 | `PKOR_Impuls1/` `KMAL_EN78-001A.s`, `KMAL_EN78-001D.s`, `KMAL_EN79-004A.s` +1 | 4 | 12 | 8 | wrong | Impuls A/D cab cars: as ED78's (same maker's interior), 8 seats by the cab, aisle into the cab |
| L037 | `PKOR_Impuls1/` `KMAL_EN78-001B.s`, `KMAL_EN79-004B.s` | 2 | 6 | 32 | drop 5-8 | Impuls B: 5-8 on their backrest (the partition panel beside them widened the bench); the others 0.05-0.1 m back from the cushion's middle |
| L038 | `PKOR_Impuls1/` `KMAL_EN78-001C.s`, `KMAL_EN79-004D.s` | 2 | 6 | 32 | drop 5-8 | Impuls C: as L037 |
| L039 | `PKOR_Impuls1/` `KMAL_EN79-004C.s` | 1 | 2 | 34 | drop 5-10 | Impuls EN79 C: 5-8 as L037, 9-10 on the vestibule partition's slanted panel |
| L062 | `PKP_113A-OR/` `PKPIC_WRbd-70162.s`, `PKPIC_WRo-70139.s`, `PKP_WRbd-70024.s` +2 | 5 | 18 | 22 | drop 17-22 | 113A WRbd/WRo restaurant: 1-16 at the dining tables; 17-18 face across the car at the bar's end, 19-22 a bench in the kitchen; no door parts |
| L063 | `PKP_113A-OR/` `PREG_BR3nopuz-71004.s` | 1 | 1 | 36 | drop 33-36 | 113A BR3 bar car: 33-36 face into their own backrest (a glass screen in front outweighed it); the bar's round stools not found |
| L076 | `KS_EN75/` `KS_EN75_A.s` | 1 | 4 | 46 | drop 1,2 | EN75 A (Elf-type): 1-2 sideways points on the edge of the raised floor over the articulation |
| L077 | `KS_EN75/` `KS_EN75_B.s` | 1 | 4 | 46 | drop 45,46 | EN75 B: as L076 at its articulated end |
| L078 | `KS_EN75/` `KS_EN75_C.s` | 1 | 4 | 20 | drop 1,2,19,20 | EN75 C: both articulated ends |
| L079 | `KS_EN75/` `KS_EN75_D.s` | 1 | 4 | 36 | drop 1,2,35,36 | EN75 D: both articulated ends |
| L040 | `PKP EN57_AA-OR/` `PKP_EN57-1752ra.s`, `PKP_EN57-895ra.s`, `PKP_EN57-895rb.s` | 3 | 7 | 41 | drop 5-16,23-24,27-30,39-41 | EN57AKŁ-type (ED72 interior) ra: merged blocks; 39-41 in the cab |
| L041 | `PKP EN57_AA-OR/` `PKP_EN57-1752s.s`, `PKP_EN57-1943s.s`, `PKP_EN57-895s.s` | 3 | 5 | 50 | drop 5-12,19-20,23-30,39-46 | EN57AKŁ-type s: as ED72 sa |
| L042 | `PKP EN57_AA-OR/` `PKP_EN57-1943ra.s`, `PKP_EN57-1943rb.s` | 2 | 2 | 41 | drop 5-16,23-24,27-30,39-41 | as L040 (1943 ra and rb, one shape) |
| L043 | `PKP EN57_AA-OR/` `PKP_EN57-1752rb.s` | 1 | 1 | 27 | right | EN57AKŁ-type rb (armchairs as ED72 ra); no cab seat |
| L052-L053 | `PKP_SA134_218M-OR/` (2 models: `PREG_SA134-014A.s`, `PREG_SA134-014B.s`) | 4 | 10 | 32-33 | right | SA134-014 A: the low-floor saloon's seats missed (two floor levels; harmless); the aisle runs into the cab (no seat there); SA134-014 B |
| L054 | `PKP_SA134_218M-OR/` `PL-PREG_SA134-011A.s`, `PREG_SA134-011A.s` | 2 | 1 | 35 | drop 1,34,35 | SA134-011 A: 34-35 two places on the driver's seat (x 0, in the cab); 1 a vestibule flip seat facing a box 0.25 m away |
| L055 | `PKP_SA134_218M-OR/` `PL-PREG_SA134-011B.s`, `PREG_SA134-011B.s` | 2 | 1 | 34 | drop 1,2 | SA134-011 B: 1-2 the driver's seat |
| L035 | `PKP_120A-OR/` `PKPPR_Bh-18045.s`, `PKPPR_Bh-18071.s`, `PKPPR_Bh-18284.s` +7 | 10 | 7 | 56 | drop 5-12,21-36,45-52 | 120A: back-to-back benches found as one; 24 of 56 left; no door parts |
| L056-L058 | `PKP_111Arow-OR/` (7 models: `PKPIC_B9nopuvz-70059.s`, `PKPIC_B9nopuvz-78007.s`, `PKPIC_Bnopuvz-70039.s` +4) | 7 | 7 | 56-72 | right | 111A B9 open coach; no door parts; 111A B7; doors D1-D4 at both ends; 111A B8; door parts at one end only (D1, D2) |
| L080 | `PKORx_MAV_wagpas/` `START_Apmz-91107.s` | 1 | 2 | 80 | wrong | MAV Apmz 1st class: single armchairs paired along the car taken as benches along it: the top row's places line up along z facing the aisle |
| L081 | `PKORx_MAV_wagpas/` `START_Bbdpmz-91407_inside.s` | 1 | 2 | 49 | drop 25,26,29,30 | MAV Bbdpmz interior (_inside): 25-26, 29-30 on the aisle line in the middle area |
| L082-L083 | `PKORx_MAV_wagpas/` (2 models: `START_Bpmz-91100.s`, `START_Wrmz-91107.s`) | 2 | 2 | 42-80 | right | MAV Bpmz: back-to-back benches split right; MAV WRmz restaurant: dining seats at tables, kitchen half without; no door parts |
| L084-L087 | `PKP_154A-OR/` (4 models: `PKPIC_B10mnopuz-90021.s`, `PKPIC_B9mnopuvz-90002.s`, `PKPIC_B9mnopuvz-90010.s` +1) | 4 | 6 | 72-80 | right | 154A B10; 154A B9 (90002); 154A B9 (90010); 154A Bdhmnu |
| L071 | `PKP_111Ainw-OR/` `PKP_Bdhinu_29-70_007.s`, `PKP_Bdhinu_29-78_008.s` | 2 | 1 | 69 | drop 69 | 111A Bdhinu/Brdhnu: 69 a flip seat on the end partition, on the aisle line |
| L072 | `PKP_111Ainw-OR/` (2 models: `PKP_Brdhnu_84-70_003.s`, `PKP_Brdhnu_84-70_009.s`) | 2 | 1 | 60 | right | 111A Brdhnu: bike/wheelchair end without seats |
| L073 | `PKP_111Ainw-OR/` `PKPIC_B8bnopuz-78004.s` | 1 | 3 | 69 | drop 69 | 111A B8bnopuz: as L071; door parts at one end only |
| L088 | `PKP_Gorlitz88-OR/` `PKPPR_Bdhpmnu-08143.s`, `PREG_B16mnopux-08063.s`, `PREG_B16mnopux-08106.s` +1 | 4 | 5 | 8 | wrong | Görlitz double-deck coach: decks not found (double-deck not handled), 8 places on side benches at one end, D2 inside the body |
| L089 | `PKP_SA103_214M-OR/` `PL-REG_SA103-004.s`, `PREG_SA103-004_w2.s`, `PREG_SA103-013.s` | 3 | 3 | 34 | drop 34 | SA103 (single-car DMU): 34 a fold seat by the toilet partition at the aisle; the other end's raised saloon missed |
| L090 | `PKP_SA103_214M-OR/` `PREG_SA103-004.s` | 1 | 2 | 32 | drop 32 | SA103-004: as L089 |
| L129 | `PKP_111A-20/` `PKPIC_B9nopuz-78102.s` | 1 | 5 | 56 | right | 111A-20 B9 (as L057) |
| L070 | `PKOR_B-162A/` (5 models: `PKPPR_B9nopuz-71001.s`, `PKPPR_B9nopuz-71002.s`, `PREG_B9nopuz-71001.s` +2) | 5 | 4 | 78 | right | B-162A (B9nopuz) open coach; doors at both ends |
| L118 | `SKPL_SD85/` (2 models: `SKPL_SD85-001_s1.s`, `SKPL_SD85-001_s2.s`) | 2 | 4 | 72 | right | SD85 s1/s2; no door parts |
| L131 | `PKP_159A-OR/` `PKPIC_B9mnopuz-90013.s` | 1 | 4 | 72 | right | 159A B9 |
| L064 | `PKP_EN57-1291/` `EN57-1291ra.s`, `EN57-1291rb.s` | 4 | 2 | 57 | drop 57 | EN57-1291 ra/rb: back-to-back benches split right; 57 in the cab; no door parts |
| L065 | `PKP_EN57-1291/` `EN57-1291s.s` | 2 | 1 | 76 | right | EN57-1291 s; no door parts |
| L066 | `PKP_EN57-787/` `EN57-787ra.s`, `EN57-787rb.s` | 4 | 2 | 57 | drop 57,d1,d2,d3,d4 | EN57-787 ra/rb: 57 in the cab; the doors come out on the car's middle line (x 0: both sides' leaves under one part), TSRE's CarDoors reads them the same at load |
| L067 | `PKP_EN57-787/` `EN57-787s.s` | 2 | 1 | 76 | drop d1,d2,d3,d4 | EN57-787 s: seats right; doors on the middle line, D3 2.8 m wide |
| L112 | `SKPL_SN84/` `SKPL_SN84-003_s1.s`, `SKPL_SN84-003_s2.s` | 2 | 2 | 84 | drop 3,6,9,12,15,83,84 | SN84 s1/s2: the left section's 3-wide benches put their inner place on the aisle (x -0.14); 83-84 in the cab |
| L113 | `SKPL_SN84/` `SKPL_SN84-003_w.s` | 1 | 1 | 58 | right | SN84 w trailer |
| L115 | `PKP_152A-OR/` (2 models: `PKPIC_A9mnopuz-90018.s`, `PKPIC_A9mnopuz-90037.s`) | 2 | 3 | 54 | right | 152A A9 1st class 2+1 (the aisle in the gap) |
| L126 | `PKOR_B-168A/` `PKPIC_B7bnopuz-70022.s` | 1 | 3 | 56 | right | 168A B7b |
| L127 | `PKORx_ARmpee832/` `CD_ARmpee832_002.s` | 1 | 3 | 34 | drop 1-12 | CD ARmpee: 1-12 places strung along curved lounge sofas (straight-bench model does not fit); 13-34 the 1st class part right |
| L130 | `PKP_158A-OR/` `PKPIC_A9mnopuz-90044.s` | 1 | 3 | 54 | right | 158A A9 1st class |
| L097 | `CD_Bdmpee/` `61_54_cz-cd_2071_009-2.s`, `61_54_cz-cd_2071_025-8.s`, `61_54_cz-cd_2071_026-6.s` | 3 | 2 | 80 | drop d2,d5 | CD Bdmpee: seats right; D2 and D5 are interior sliding doors across the middle seats (on the plan inside the body) |
| L099 | `PKOR_AB-163A/` (3 models: `PKPPR_AB8nopuz-71001.s`, `PREG_AB8nopuz-71001.s`, `PREG_AB8nopuz-71003_w2.s`) | 3 | 2 | 69 | right | AB-163A: 2nd class 2+2 and 1st class 2+1 (aisle in the gap) |
| L100 | `PKOR_B-161A/` (3 models: `PKPPR_B6bnopuvz-71001.s`, `PREG_B6bnopuvz-71001.s`, `PREG_B6bnopuvz-71004_w2.s`) | 3 | 2 | 58 | right | B-161A |
| L101 | `PKOR_Energetyka/` `PKPE_S-1223.s`, `PKPE_S-1224.s` | 2 | 0 | 16 | none | PKOR_Energetyka S-1223/1224 (the PPS shape): service coach, not a passenger vehicle |
| L102 | `PKOR_Energetyka/` `PKPE_S-1229.s` | 1 | 2 | 22 | none | PKOR_Energetyka S-1229 (the restaurant shape): service coach |
| L114 | `PKOR_BR-113A/` `PREG_BR3nopuz-71003_w2.s`, `PREG_BR3nopuz-71004.s` | 2 | 2 | 36 | drop 33-36 | BR3nopuz (PKOR_BR-113A copy of L063): as L063 |
| L122 | `DB_Wawel/` `DB_Bpmz294.s` | 1 | 2 | 80 | right | DB Bpmz (Wawel); no door parts |
| L125 | `PKOR_B-111Arow/` `PKPIC_B7nopuvz-70072.s` | 1 | 2 | 56 | right | 111A B7 (as L057) |
| L136 | `PKP_Bautzen89-OR/` `PKPIC_WRmnouz-70190.s` | 1 | 2 | 62 | wrong | Bautzen WRmnouz restaurant: the chairs at 2-person tables read as benches along the car (one side faces across it), 1-4 on the aisle by the bar |
| L001-L003 | `Pakiet_101A_102A-OR/` (7 models: `101A_2KL_D.s`, `101A_2KL_1.s`, `101A_2KL_2.s` +4) | 37 | 0 | 44-76 | right | 101A 2nd class open: bays, aisle end to end, toilet box each saloon; no door parts (TSRE guesses); 101A 1st class 2+1, sides swap at the middle; no door parts; 101A 2nd FLIP variant (box moved); no door parts |
| L004 | `Pakiet_101A_102A-OR/` `PKP_101A_G.s` | 1 | 1 | 20 | none | PKP_101A_G.wag typed Freight (static/derelict 101A, half its seats): not a passenger car, left out |
| L120 | `CD_Ampz146_003/` `cd_Ampz146_003.s` | 1 | 1 | 86 | drop 2,5,9,13,17,21,25,29,33,37,41,45,49,53,57,61,65,69,73,77,81,85 | CD Ampz 1st class 2+1: the middle of three places on each pair of armchairs |
| L128 | `PKORx_RJ_wagpas/` `RJ_WRmz-214.s` | 1 | 1 | 40 | right | RJ WRmz restaurant: chairs facing tables (the "facing" flags are table sides); kitchen half without |
| L133 | `PKP_163A-OR/` `PKPPR_AB8nopuz-71001.s` | 1 | 1 | 69 | right | 163A (as L099) |
| L005 | `PKP_ED72/` `PKPPR_ED72-006sa.s`, `PKPPR_ED72-006sb.s`, `PKPPR_ED72-014sa.s` +7 | 10 | 0 | 50 | drop 5-12,19-20,23-30,39-46 | ED72 sa/sb: back-to-back benches found as one (seats on the backrest, one way); 24 of 50 left; doors D1-D4 at the vestibules |
| L006 | `PKP_ED72/` (5 models: `PKPPR_ED72-006ra.s`, `PKPPR_ED72-014ra.s`, `PKPPR_ED72-016ra.s` +2) | 5 | 0 | 29 | right | ED72 ra: armchairs; cab has none found |
| L007 | `PKP_ED72/` `PKPPR_ED72-006rb.s`, `PKPPR_ED72-014rb.s`, `PKPPR_ED72-016rb.s` +2 | 5 | 0 | 39 | drop 5-16,23-24,27-30,35-39 | ED72 rb: merged blocks; 35-36 side bench in the staff room, 37-39 in the cab (driver's seat 39); 16 left |
| L008 | `PKP_ED72/` (2 models: `PKPPR_ED72-012ra.s`, `PKPPR_ED72-013ra.s`) | 2 | 0 | 29 | right | ED72-012/013 ra; no door parts |
| L009 | `PKP_ED72/` `PKPPR_ED72-012rb.s`, `PKPPR_ED72-013rb.s` | 2 | 0 | 39 | drop 5-16,23-24,27-30,35-39 | as L007; no door parts |
| L010 | `PKP_ED72/` `PKPPR_ED72-012sa.s`, `PKPPR_ED72-013sa.s` | 2 | 0 | 50 | drop 5-12,21-28,31-32,39-46 | as L005; no door parts |
| L011 | `PKP_ED72/` `PKPPR_ED72-012sb.s`, `PKPPR_ED72-013sb.s` | 2 | 0 | 50 | drop 5-12,19-20,23-30,39-46 | as L005; no door parts |
| L012 | `PKP_ED72/` `PKPPR_ED72-021sa.s`, `PKPPR_ED72-021sb.s` | 2 | 0 | 50 | drop 5-12,19-20,23-30,39-46 | as L005 |
| L013 | `PKP_ED72/` `PKPPR_ED72-021ra.s` | 1 | 0 | 29 | right | as L006 |
| L014 | `PKP_ED72/` `PKPPR_ED72-021rb.s` | 1 | 0 | 39 | drop 5-16,23-24,27-30,35-39 | as L007 |
| L015 | `PKP_EN57/` (5 models: `PKP_EN57-1326s.s`, `PKP_EN57-1575s.s`, `PKP_EN57-1827s.s` +2) | 5 | 0 | 76 | right | PKP_EN57 copies of PKP_EN57-OR's older EN57 s (shapes byte-identical) |
| L016 | `PKP_EN57/` `PKP_EN57-1827ra.s`, `PKP_EN57-907ra.s`, `PKP_EN57-945ra.s` | 3 | 0 | 58 | drop 57,58 | older EN57 ra: 57-58 the cab's crew bench (also in PKP_EN57-OR's written .sd) |
| L017 | `PKP_EN57/` `PKP_EN57-1827rb.s`, `PKP_EN57-907rb.s`, `PKP_EN57-945rb.s` | 3 | 0 | 58 | drop 1,2 | older EN57 rb: 1-2 the cab's crew bench |
| L018 | `PKP_EN57/` `PKP_EN57-1326ra.s`, `PKP_EN57-1575ra.s` | 2 | 0 | 58 | drop 57,58 | as L016 |
| L019 | `PKP_EN57/` `PKP_EN57-1326rb.s`, `PKP_EN57-1575rb.s` | 2 | 0 | 58 | drop 1,2 | as L017 |
| L020-L022 | `PKP_EN57/` (3 models: `PKP_EN57-857ra.s`, `PKP_EN57-857rb.s`, `PKP_EN57-857s.s`) | 3 | 0 | 52-76 | right | EN57-857 ra (no cab seat found); EN57-857 rb; EN57-857 s (same result as EN57SPOT s, L025) |
| L028 | `ED78_31WE/` `PREG_ED78-024_D.s` | 3 | 0 | 8 | wrong | ED78 D (cab end): two floor levels; the floor found is the raised one by the cab: 8 seats there, the aisle into the cab, the saloon's seats missed |
| L029 | `ED78_31WE/` `PREG_ED78-024_B.s` | 2 | 0 | 32 | right | ED78 B: low-floor saloon; the raised ends' seats missed (harmless) |
| L030 | `ED78_31WE/` `PREG_ED78-024_C.s` | 2 | 0 | 34 | drop 33,34 | ED78 C: 33-34 sideways on the step to a raised end |
| L031 | `ED78_31WE/` `PREG_ED78-018_A.s` | 1 | 0 | 8 | wrong | as L028 |
| L032-L033 | `ED78_31WE/` (2 models: `PREG_ED78-018_B.s`, `PREG_ED78-018_C.s`) | 2 | 0 | 40 | right | ED78-018 B; two doors only (D1, D2); ED78-018 C |
| L034 | `ED78_31WE/` `PREG_ED78-018_D.s` | 1 | 0 | 8 | wrong | as L028 |
| L059-L061 | `PKOR_EN57AL/` (6 models: `KD_EN57AL-1542ra.s`, `KD_EN57AL-1542ra_w2.s`, `KD_EN57AL-1542rb.s` +3) | 6 | 0 | 34-76 | right | EN57AL 1542 ra (KD); seats 33-34 in the multi-purpose room; EN57AL 1542 rb; EN57AL 1542 s |
| L068-L069 | `PKP_SA133_218M-OR/` (6 models: `PREG_SA133-001A.s`, `PREG_SA133-004A.s`, `PREG_SA133-006A.s` +3) | 6 | 0 | 31-34 | right | SA133 A: as SA134-014 A; SA133 B |
| L074 | `PKP_PPS-OR/` `OR_PKP_PPS_1.s`, `OR_PKP_PPS_2.s`, `PKPE_PPS-1223.s` +1 | 4 | 0 | 16 | none | PKP_PPS-OR service coach (Energetyka crew room); no train in PSK_2026: left out |
| L075 | `PKP_PPS-OR/` `PKPE_PPS-1229.s` | 1 | 0 | 22 | none | PKPE_PPS-1229: the 113A restaurant shape reused for a service train: left out |
| L091 | `PKP_SA134/` `PL-PREG_SA134-011A.s` | 1 | 0 | 35 | drop 1,34,35 | as L054 (PKP_SA134 copy) |
| L092 | `PKP_SA134/` `PL-PREG_SA134-011B.s` | 1 | 0 | 34 | drop 1,2 | as L055 |
| L093-L094 | `PKP_SA134/` (2 models: `PREG_SA134-014A.s`, `PREG_SA134-014B.s`) | 2 | 0 | 32-33 | right | as L052; as L053 |
| L095-L096 | `PKP_SA139_223M-OR/` (4 models: `PREG_SA139-001A.s`, `PREG_SA139-024A.s`, `PREG_SA139-001B.s` +1) | 4 | 0 | 28 | right | SA139 A: low-floor part's seats missed; no door parts; SA139 B; no door parts |
| L098 | `PKOR_810/` `SKPL_810-569.s`, `SKPL_810-569_bm.s`, `SKPL_810-569_wag.s` | 3 | 0 | 52 | drop 1,52 | 810 railbus (3+2): 1 and 52 the drivers' seats at both cabs; back-to-back benches split right |
| L103 | `PKP_401Z/` `401Zp_g.s` | 3 | 0 | 9 | none | 401Zp_g: a freight wagon's load (vehicle typed Freight): not seats |
| L104 | `PKP_SA103/` `PL-REG_SA103-004.s`, `PREG_SA103-013.s` | 2 | 0 | 34 | drop 34 | as L089 |
| L105 | `PKP_SA103/` `PREG_SA103-004.s` | 1 | 0 | 32 | drop 32 | as L090 |
| L106 | `PKP_SA110/` `PKP_SA110-11.s` | 1 | 0 | 83 | drop 1-16,83 | SA110-11: the end section's benches run across the car (four places on one side, the aisle through 3-4, 7-8...); 83 the driver's seat; no door parts |
| L107 | `PKP_SA110/` `PKP_SA110-12.s` | 1 | 0 | 83 | drop 1,68-83 | SA110-12: mirrored (1 the driver's seat, 68-83 the end section) |
| L108 | `PKP_SA110/` `PKP_SA112-03.s` | 1 | 0 | 88 | right | SA112 trailer; no door parts |
| L109-L111 | `PKP_SA136_219M-OR/` (3 models: `PREG_SA136-005A.s`, `PREG_SA136-005B.s`, `PREG_SA136-005C.s`) | 3 | 0 | 21-40 | right | SA136 A: low-floor part missed; SA136 B: the aisle line reaches onto the raised floors over the bogies (no seats there); SA136 C |
| L116 | `PKP_162A-OR/` (2 models: `PKPPR_B9nopuz-71001.s`, `PKPPR_B9nopuz-71002.s`) | 2 | 0 | 78 | right | 162A B9 (as L070) |
| L117 | `PKP_401Z-OR/` `PKPC_Eamos-889_g.s` | 2 | 0 | 9 | none | 401Z Eamos load: not seats (freight) |
| L119 | `cd_WRmz_006/` `cd_WRmz_006.s` | 2 | 0 | 46 | drop 2,6,10,14,18,22,26,30,34,38,41-46 | CD WRmz restaurant: three places on each 2-person bench (the middle one, 0.38 m apart), 41-46 lounge chairs set diagonally round a table (the finder knows four headings) |
| L121 | `CD_WRmz815_010/` `cd_WRmz815_010.s` | 1 | 0 | 46 | drop 2,6,10,14,18,22,26,30,34,38,41-46 | as L119 |
| L123 | `EN76_22WEa/` `KW_EN76-035_D.s` | 1 | 0 | 22 | drop 21,22 | EN76 D (KW): 21-22 sideways at the articulation (as EN75); the cab half's seats missed |
| L124 | `H-START CAF Minipack/` `H-START_61_55_20-91_111-1.s` | 1 | 0 | 80 | right | CAF Minipack (H-START): as L082 |
| L132 | `PKP_161A-OR/` `PKPPR_B6bnopuvz-71001.s` | 1 | 0 | 58 | right | 161A (as L100) |
| L134 | `PKP_168A-OR/` `PKPIC_B7bnopuz-70022.s` | 1 | 0 | 56 | right | 168A (as L126) |
| L135 | `PKP_304C-OR/` `PKP_Pdn-48296.s` | 1 | 0 | 23 | none | 304C Pdn crew car: wall benches; no train; a service car (as the PPS) |
| L137 | `cd_043/` `cd_043003.s` | 1 | 0 | 72 | right | cd_043 trailer |
| L138 | `cd_843-024/` `cd_843024ms.s` | 1 | 0 | 54 | drop d2,d3 | cd 843 railcar: seats right; D2 and D3 inside the body (the luggage room's inner doors) |
| L139 | `cd_854_054_954/` `CD954201rv.s` | 1 | 0 | 67 | drop 2,6,10,14,18 | cd 854/954 rv: the 1st class section's middle of three places on each pair of armchairs; 60-67 the luggage room's wall seats kept |
| L140 | `cd_Ampz_002/` `cd_Ampz_002.s` | 1 | 0 | 86 | drop 2,5,9,13,17,21,25,29,33,37,41,45,49,53,57,61,65,69,73,77,81,85 | as L120 |

### Cars without seats, by reason
Passenger models without 8 seats, outside the two EN57 folders (513 models):

| Why | Models | Vehicle files | Train uses | Most used (train uses) |
|---|---|---|---|---|
| attachment or part: a shadow, a driver, a coupler (no car body) | 215 | 752 | 661 | `common.fa` (219), `PKP_ED250-OR` (120), `PKP_111A-OR` (91), `PKP_112A-OR` (44), `PKP_Bipa_Bhp_VD-OR` (36), `PKP_113A-OR` (18) |
| no aisle between the benches | 143 | 147 | 194 | `PKP_112A-OR` (48), `PKP_111A-OR` (19), `PKP_Bipa_Bhp_VD-OR` (18), `CD_Bmz234` (14), `PKP_ED250-OR` (12), `PKOR_B-111A` (9) |
| seats on one side only | 78 | 81 | 109 | `PKP_111A-OR` (72), `zssk_B` (8), `PKP_YB-OR` (5), `PKOR_EN57AL` (4), `PKP_YB86-OR` (3), `CD_B` (3) |
| no cushions | 74 | 77 | 62 | `PKP_ED250-OR` (42), `PKP_Bhp-OR` (8), `PKOR_EN57AL` (2), `DB_Wawel` (2), `PKORx_MAV_wagpas` (2), `cd_Bt_160` (2) |
| fewer than 8 seats | 3 | 3 | 18 | `PKOR_Elf1` (18): the Elf A cars |

What they are (plans looked at for each family above, and for one model of
each family staged with doors only):

- **Compartment cars**, right to have none (the README's limits): 111A
  (`PKP_111A-OR`, the timetable's most used coach: compartments with the
  corridor along one side, "seats on one side only"), 112A (compartments,
  three seats a bench across), 110A, 136A, 139A-141A, 145A, 155A, 174A, Z1A,
  Z1B, Z2A, Z2B, XB, XBN, Görlitz 90, cd_Bmz, CAF Minipack 102-3 and the
  compartment half of 141A-20 and RJ Ampz-013 (their open halves missed with
  them).
- **Sleepers and couchettes** (305A, 306A, YB, YB79, YB86: berths along one
  side) and **restaurant cars** with tables on one side (113A WRnouz, 406A):
  "seats on one side only".
- **No interior modelled**: ED250 (some 290 triangles inside the body),
  `PKP_Bhp-OR`, the MAV Bbdpmz shell (its interior is `_inside`, L081),
  Pakiet 101A/102A's car shapes (the interiors are their own models, L001-L003).
- **A simple interior**: `PKP_Bipa_Bhp_VD-OR` (73 models, 153 vehicle files):
  benches as thin panels across the car, no cushion faces.
- **The finder missed them**: the EN57AL `PKOR_EN57AL/PREG_EN57AL-2103*`
  (9 models): cushions 0.29-0.31 m above the floor it found, the window
  starts at 0.33 m (rule 12); the Elf A cars (`KSL_EN76-00xA`, 18 train uses):
  2 seats found, two floor levels (rule 5).

### Doors
- **Staged with doors only**: 145 passenger models with door parts and no
  usable seats (compartment cars, sleepers, restaurant cars, no interior;
  148 vehicle files, 145 train uses). Their doors measured and looked at: all
  on the car's sides at the vestibules.
- **Left out**: `PKP_WM15A-OR/PKP_WM15A-266.s` (a track trolley, one door
  inside the body) and `PKP_SR51-OR/PKP_SR51-02_F.s` (doors on the middle
  line, 3.8 m wide).
- **Doors found wrongly**, taken out with `drop`: EN57-787 (`d1`-`d4`: on the
  car's middle line, x 0, D3 2.8 m wide: both sides' leaves are under one
  part), CD Bdmpee (`d2`, `d5`: interior sliding doors across the middle
  seats), cd 843 (`d2`, `d3`: the luggage room's inner doors). With the Door
  lines gone TSRE reads the door parts at load and gets EN57-787's the same
  wrong way (rule 13).
- **Door parts at one end only**: 111A B8nopuvz (L058), 111Ainw B8bnopuz
  (L073), ED78-018 (one pair).
- **Staged with seats but without door parts** (TSRE guesses their doors): 50
  models: Pakiet 101A/102A (7), ED72-012/013 (8), 120A (10), 111A B9 (5),
  113A restaurant (5), EN57-1291 (3), SA139 (4), SA110 (3), SD85 (2), MAV
  (2), DB Wawel (1).
- Passenger cars without seats and without door parts (most of the 513
  above, 111A and 112A among them) keep TSRE's guess too.

## MaSzyna

### Interiors with seats found
Most used first (vehicle files: the `.mmd` files using the model; MaSzyna's
cars are not in PSK_2026). A MaSzyna car's seats are in its interior
(`lowpolyinterior:`); doors are not written (TSRE reads them by the door
prefix).

| Layout | Model (or family) | Vehicle files | Seats | Verdict | Notes |
|---|---|---|---|---|---|
| M001-M019 | `pkp/en57_v1/` (23 models: `6bbii_lowpoly.e3d`, `6bbiii_lowpoly.e3d`, `6baii_lowpoly.e3d` +20) | 131 | 45-76 | right | EN57 v1 lowpoly interior: bays, aisle end to end, no cab seat; luggage-room wall seats kept |
| M020-M051 | `pkp/impuls_v1/` (36 models: `int_45we-bd_kmal.e3d`, `int_45we-c_kmal.e3d`, `int_36wea-a.e3d` +33) | 48 | 9-44 | right | Impuls interior: the low-floor saloons; the raised ends and the cab missed |
| M063 | `pkp/en57al_v1/` `al-km_ra-lowpoly.e3d` | 3 | 51 | drop 49-51 | EN57AL km ra: 49-51 three folding seats on the wall facing across, read as facing along the car |
| M064 | `pkp/en57al_v1/` `al-km_rb-lowpoly.e3d` | 3 | 33 | drop 3-5 | EN57AL km rb: 3-5 as M063 |
| M065-M078 | `pkp/en57al_v1/` (16 models: `al-km_s-lowpoly.e3d`, `al-alcd_ra-lowpoly.e3d`, `al-alc_warmaz_ra-lowpoly.e3d` +13) | 24 | 28-76 | right | EN57AL lowpoly interior |
| M052-M054 | `pkp/elf_v1/` (5 models: `int_22wee-b.e3d`, `int_22wec-b.e3d`, `int_22wec-c.e3d` +2) | 13 | 16-30 | right | Elf interior |
| M055 | `pkp/elf_v1/` `elf_intsrkab_a.e3d` | 2 | 30 | drop 1,2 | Elf (srkab A): 1-2 sideways points at the raised floor over the articulation |
| M056 | `pkp/elf_v1/` `int_22we-006-d.e3d` | 2 | 12 | right | Elf interior |
| M057 | `pkp/elf_v1/` `int_27we-b.e3d`, `int_27we-e.e3d` | 2 | 16 | drop 1,2,15,16 | Elf 27WE B (SKM): longitudinal wall benches right; 1-2, 15-16 sideways at the articulations |
| M058 | `pkp/elf_v1/` `int_21we-b.e3d` | 1 | 40 | drop 1,2,39,40 | Elf 21WE B: 1-2, 39-40 sideways at the articulations |
| M059-M062 | `pkp/elf_v1/` (4 models: `int_22wea-b.e3d`, `int_22wea-c.e3d`, `int_27web-c.e3d` +1) | 4 | 24-36 | right | Elf interior |
| M079-M086 | `pkp/en57-2000_v1/` (11 models: `6bs_lowpoly.e3d`, `6bs_lowpoly_nm.e3d`, `6bs_ks-lowpoly.e3d` +8) | 24 | 41-76 | right | EN57 (2000s refurbishment) lowpoly interior |
| M087 | `pkp/120a_v1/` `int_120a.e3d` | 9 | 34 | wrong | 120A int_120a: back-to-back benches found as one (32 of 34 seats on the backrests) |
| M088-M089 | `pkp/120a_v1/` (3 models: `int_120a_row_1.e3d`, `int_120a_row_2.e3d`, `int_120a_row_3.e3d`) | 3 | 48-72 | right | 120A interior (pairs both sides of a backrest split right) |
| M090 | `pkp/120a_v1/` `int_120a_turkol.e3d` | 1 | 18 | wrong | 120A turkol: as M087; 18 on a toilet |
| M091-M093 | `pkp/111arow_v1/` (4 models: `int_111arow_i.e3d`, `int_111arow_iii.e3d`, `int_111arow_ii.e3d` +1) | 4 | 66-72 | right | 111A interior |
| M094 | `pkp/111arow_v1/` `int_111arow_iv_kino.e3d` | 1 | 120 | wrong | 111A kino: every cushion gives two places 0.33 m apart, one facing into its own backrest |
| M095 | `pkp/111arow_v1/` `int_111arow_v.e3d` | 1 | 62 | drop 1,62 | 111A v: 1 and 62 on the end rooms' toilets |
| M096 | `pkp/111arow_v1/` `int_111arow_vi.e3d` | 1 | 61 | drop 61 | 111A vi: 61 on a toilet |
| M097-M098 | `pkp/sa134_v1/` (4 models: `wnetrze.e3d`, `wnetrze_arp.e3d`, `wnetrzeb.e3d` +1) | 6 | 33 | right | SA134 interior (one saloon; no cab seat) |
| M099-M102 | `pkp/en71aks_v1/` (4 models: `aks_ra-lowpoly.e3d`, `aks_rb-lowpoly.e3d`, `aks_sa-lowpoly.e3d` +1) | 4 | 28-76 | right | EN71AKŚ lowpoly |
| M103 | `pkp/sn61_v2/` `61]lowpoly.e3d` | 4 | 32 | wrong | SN61 lowpoly: back-to-back benches found as one (most seats on the backrests) |
| M104 | `pkp/14xa_v1/` `int_111a-20.e3d` | 2 | 74 | drop 1,74 | 111A-20 (14xa): 1 and 74 on the end toilets |
| M105 | `pkp/14xa_v1/` `int_141a-20.e3d` | 1 | 50 | drop 1,50 | 141A-20 (14xa): 1 and 50 on the end toilets; the compartments without seats |
| M106-M108 | `pkp/en57akl_v1/` (3 models: `akl_ra-lowpoly.e3d`, `akl_rb-lowpoly.e3d`, `akl_s-lowpoly.e3d`) | 3 | 36-76 | right | EN57AKŁ lowpoly |
| M109-M111 | `pkp/ew58_v1/` (3 models: `ew58-d.e3d`, `ew58-sa.e3d`, `ew58-sb.e3d`) | 3 | 64-80 | right | EW58 |
| M112-M114 | `pkp/ew60_v1/` (3 models: `ew60-ra.e3d`, `ew60-rb.e3d`, `ew60-s.e3d`) | 3 | 44-64 | right | EW60 (the .mmd has no passenger data) |
| M116-M117 | `pkp/120a_ng_v1/` (2 models: `int_120a_ng_row_1_o1.e3d`, `int_120a_ng_row_2_o6.e3d`) | 2 | 56-72 | right | 120A NG |
| M118 | `pkp/168a_v1/` `int_168a.e3d` | 1 | 59 | drop 1,58,59 | 168A: 1, 58-59 in the end rooms (toilets) |
| M119 | `pkp/168a_v1/` `int_168ar.e3d` | 1 | 60 | right | 168A |
| M120 | `pkp/sr61_v2/` `61]lowpoly.e3d` | 2 | 32 | wrong | SR61 (the SN61 model): as M103 |
| M121 | `pkp/z2_v1/` `int_b89_rest_ns3.e3d` | 2 | 39 | right | B89 restaurant (tables) |
| M122 | `pkp/113aa_v1/` `int_113aa_pr.e3d` | 1 | 36 | right | 113AA |
| M115 | `pkp/11xa_v2/` `int_113a_braz.e3d` | 1 | 23 | wrong | 113A bar car (braz): 4-5 on the aisle by the bar, the bar part across the aisle |
| M123 | `pkp/204v_v1/` `204v_sarkom.e3d` | 1 | 8 | none | 204V sarkom: service car, sideways seats; not a passenger vehicle |
| M124 | `pkp/59ws_v1/` `592ws.e3d` | 1 | 12 | none | 59WS: service car; not a passenger vehicle |
| M125 | `pkp/en57km_v1/` `en71km_lowpoly_ra.e3d` | 1 | 68 | right | EN71/EN57 KM lowpoly ra |

### Without seats, by reason
Passenger models without 8 seats, outside `pkp/en57_v2`: 275 car models
(the outside: no seats by design) and 141 interiors:

| Interiors | Why | Most |
|---|---|---|
| 95 | no aisle between the benches | compartment coaches: `11xa_v2` (30), `z2_v1` (16), `impuls_v1` (14: cab cars, two floor levels), `14xa_v1` (9), `wlab_v1` (6) |
| 21 | seats on one side only | couchettes and compartments with a side corridor: `yb_v1` (10), `110a_v2` (3), restaurant `113a_pkppr` |
| 14 | fewer than 8 | `elf_v1` (6: cab cars, two floor levels), `impuls_v1` (2), `bhp_v1` (2) |
| 11 | no cushions | `en57al_v1` (3), `16xa_v1` (2), `120a_v1` (2), `elf_v1` (2) |

## What the finder gets wrong
Grouped, each with an example (its plan's numbers) and the rule to change.
Counts are the layouts (models) it spoils here.

1. **Back-to-back benches taken as one bench**: the cushions of two benches
   meet under their common backrest (gap under 0.12 m), the group gets one row
   of places on the backrest line, all facing one way, half the places
   missing. ED72 sa `PKPPR_ED72-006sa.s` (L005): 5-12, 19-20, 23-30, 39-46 of
   50 ([plan](11-plans/or-ed72-sa-merged-benches.png)); also 120A (L035),
   the EN57AKŁ-type interior (`PKP EN57_AA-OR`, L040-L042), MaSzyna
   `int_120a` (M087, M090) and SN61/SR61 (M103, M120); 15 layouts, 46 models.
   *Rule*: split a bench whose cushion has a backrest across its middle into
   two, each facing away from it (the finder already does it for EN57-1291 and
   MAV Bpmz, whose cushions do not touch).
2. **Crew seats in a cab** (parts not named for a cab): older EN57 ra 57-58 /
   rb 1-2 ([plan](11-plans/or-en57-ra-cab-bench.png), also in
   `PKP_EN57-OR`'s written `.sd`), ED72 rb 37-39 (35-36 the staff room),
   EN57AKŁ-type ra 39-41, EN57-1291/787 57, SA134-011 34-35 / 1-2 (two places
   on the driver's seat, x 0), SA110 83 / 1, SN84 83-84, 810 1 and 52,
   ED160 b 49; 11 families. *Rule*: leave out seats in the last room of a
   driving end (behind the last partition, within some 2.5 m of the model's
   end), or seats at x 0 there.
3. **Three places on two armchairs** (first class, dining benches: places
   0.45 m wide on a 1.15-1.3 m pair): ED160 c 2, 6, 10... (L046), CD Ampz
   (L120, L140), cd 854 (L139), CD WRmz (L119, L121), ED160 b
   ([plan](11-plans/or-ed160-b-armchairs.png)); 7 layouts. *Rule*: count
   places by the cushion's pieces (gaps between armchairs), or 0.55 m a place
   when a bench is made of separate cushions.
4. **2+1 seating that swaps sides mid-car**: one straight aisle through both
   saloons runs over the armchairs of one of them. ED160 b (L045, wrong: 37,
   41, 45 on the aisle). *Rule*: an aisle per saloon (between passages); the
   block takes several `Aisle` lines.
5. **Two floor levels** (a low-floor middle, raised ends): the floor with
   the most area wins. Cab cars get a few seats on the raised end by the cab
   and the aisle runs into the cab, away from the doors: ED78 D
   `PREG_ED78-024_D.s` (L028, [plan](11-plans/or-ed78-d-two-floors.png)),
   ED78 A (L031, L034), Impuls A/D (L036, 12 train uses), MaSzyna Impuls and
   Elf cab-car interiors, the Elf A cars (2 seats). Middle cars only miss
   the raised ends' seats (harmless: ED78, Impuls, SA13x). *Rule*: find
   benches per floor level and take the level of the doors' thresholds for the
   aisle.
6. **Sideways points on the edge of a raised floor** (the floor over an
   articulation, at cushion height, taken as a cushion): EN75 A 1-2 (L076),
   EN75 B/C/D, EN76 D 21-22, MaSzyna Elf 1-2 (M055, M057, M058). *Rule*:
   leave out cushion groups deeper than about 1.2 m or reaching the car's
   width.
7. **Seats along the car read the wrong way**: single armchairs paired along
   the car taken as a bench along it (places along z facing the aisle): MAV
   Apmz (L080, wrong, [plan](11-plans/or-mav-apmz-benches-along.png)); and
   the opposite: three folding seats on the wall, facing across, read as
   facing along the car: MaSzyna EN57AL km ra 49-51
   ([plan](11-plans/maszyna-en57al-km-ra-wall-seats.png)), rb 3-5. *Rule*:
   take a bench's direction from its backrest (which side it stands on), not
   from the cushion's proportions.
8. **Panels, ledges and stairs at cushion height**: a partition's slanted
   panel beside a seat widens its bench so the place lands on the backrest
   (Impuls B 5-8, [plan](11-plans/or-impuls-b-partition-panel.png)), or is a
   seat itself (Impuls EN79 C 9-10); stair treads up to a raised floor (ED160
   a 41-44); flip seats on an end partition, on the aisle line (111A
   Bdhinu/B8b 69, SA103 34/32, SN84 3, 6, 9, 12, 15 at x -0.14). *Rule*: a
   place needs its backrest within the cushion's depth behind it; leave out
   places on the aisle line and step-like runs of small faces.
9. **Facing into the own backrest**: the back is the side with more upright
   area within 0.25 m, so a glass screen in front outweighs a thin backrest:
   113A BR3 bar car 33-36 (L063, L114,
   [plan](11-plans/or-113a-br3-facing-backrest.png)); MaSzyna 111A kino:
   every cushion gives two places 0.33 m apart, one facing its backrest (M094,
   wrong). *Rule*: the back is the upright face adjoining the cushion's edge.
10. **Toilets and end rooms**: single places on a toilet in an end room:
    MaSzyna 111A v 1, 62 (M095), vi 61, 14xa 1, 74 / 1, 50, 168A 1, 58-59,
    120A turkol 18. *Rule*: leave out single cushions in rooms shorter than
    1.5 m at a car's end.
11. **Restaurant and bar cars**: lounge chairs set diagonally round a table
    (CD WRmz 41-46; four headings only), places strung along curved sofas (CD
    ARmpee 1-12), chairs at two-person tables read as benches along the car
    (Bautzen WRmnouz, wrong), a bench in the kitchen (113A WRbd 19-22), bar
    stools not found. *Rule*: perhaps leave restaurant and bar cars out
    unless a person maps them; or more headings than four.
12. **Cushions just under the window**: EN57AL PREG 2103 (9 models): cushion
    tops 0.29-0.31 m above the floor found (window 0.33-0.58 m), no seats.
    *Rule*: start the window at 0.27 m, or take the floor as the lowest level
    with a large area.
13. **Doors**: both sides' leaves under one door part give doors on the car's
    middle line (EN57-787,
    [plan](11-plans/or-en57-787-doors-middle.png); TSRE's `CarDoors` splits
    by part letter the same way at load); interior sliding doors named
    `DOOR_x` (CD Bdmpee d2/d5, cd 843 d2/d3). *Rule* (finder and `CarDoors`):
    split a door part's triangles by the sign of x, not by its letter; leave
    out pieces more than 0.3 m inside the body's side.
14. **The aisle off the middle**: the raised floors over the bogies count as
    benches and move the aisle 0.23 m towards one side (Elf B, L026; still in
    the gap). *Rule*: leave cushion groups without a backrest out of the aisle.

Limits met, not errors: compartment cars, sleepers, double-deck cars
(Görlitz 88, L088: 8 places on side benches at one end, decks not found).

## Plans
In [11-plans/](11-plans/), from the scan (numbered as `write` numbers them):

| Plan | What |
|---|---|
| [or-ed72-sa-merged-benches.png](11-plans/or-ed72-sa-merged-benches.png) | rule 1: ED72 sa, the merged benches' seats on the backrests (5-12...) |
| [or-en57-ra-cab-bench.png](11-plans/or-en57-ra-cab-bench.png) | rule 2: older EN57 ra, 57-58 in the cab (the written `PKP_EN57-OR` files too) |
| [or-ed160-b-armchairs.png](11-plans/or-ed160-b-armchairs.png) | rules 3, 4: ED160 b, three places on two armchairs, the aisle through the right saloon |
| [or-ed78-d-two-floors.png](11-plans/or-ed78-d-two-floors.png) | rule 5: ED78 D, 8 seats by the cab, the aisle into the cab |
| [or-mav-apmz-benches-along.png](11-plans/or-mav-apmz-benches-along.png) | rule 7: MAV Apmz, armchairs read as benches along the car |
| [maszyna-en57al-km-ra-wall-seats.png](11-plans/maszyna-en57al-km-ra-wall-seats.png) | rule 7: MaSzyna EN57AL km ra, wall seats 49-51 facing along the car |
| [or-impuls-b-partition-panel.png](11-plans/or-impuls-b-partition-panel.png) | rule 8: Impuls B, 5-8 on their backrests |
| [or-113a-br3-facing-backrest.png](11-plans/or-113a-br3-facing-backrest.png) | rule 9: 113A BR3, 33-36 facing their backrests |
| [or-en57-787-doors-middle.png](11-plans/or-en57-787-doors-middle.png) | rule 13: EN57-787 s, doors on the middle line |
| [or-ed72-sa-staged.png](11-plans/or-ed72-sa-staged.png) | the staged result: ED72 sa from its staged `.sd` (24 seats, 4 doors) |

## The staged set
In `~/humans-11/`:

| Set | Staging folder | Apply | Originals | `.sd` | with seats | with doors | doors only | new `.sd` |
|---|---|---|---|---|---|---|---|---|
| OR_PSK_NEW | `stage-or/` | `OR_PSK_NEW-sd-seats-map-20261113.zip` | `OR_PSK_NEW-sd-seats-map-20261113-backup.zip` | 381 | 236 | 328 | 145 | 344 |
| MaSzyna | `stage-maszyna/` | `maszyna-sd-seats-map-20261113.zip` | `maszyna-sd-seats-map-20261113-backup.zip` (empty: no interior had an `.sd`) | 137 | 137 | 0 | 0 | 137 |

- OR: 10 680 seats and 1 200 doors in 381 files (*with doors* counts the
  doors-only files too; 53 files with seats have no doors: the 50 models
  without door parts and EN57-787's three, whose doors were taken out);
  staged in 10 min 3 s (`write` 397 s, `doors` 195 s, the drops and
  archives the rest). MaSzyna: 6 429 seats in 137 files, 64 s. Nothing in
  `~/ORTS` changed (no file newer than a marker made before staging).
- Apply: extract the zip into the game folder (`~/ORTS/OR_PSK_NEW`,
  `~/ORTS/maszyna`); each holds a `README.md` and `NEW-SD.txt` (the `.sd`
  files it makes new).
- Undo: extract the backup, delete the files in `NEW-SD.txt`.
- The plans of every staged model, drawn from its staged `.sd` (`plan --sd`):
  `plans-staged/or/`, `plans-staged/maszyna/`, a folder each per vehicle
  folder. The scans: `scan-or/`, `scan-maszyna/` (`report.md`, `found.json`,
  `plans/`); the verdicts per layout: `verdicts-or.txt`,
  `verdicts-maszyna.txt`; the steps' own archives: `steps-or/`,
  `steps-maszyna/`.
- Left out: the EN57s and EN57ALs of `PKP_EN57-OR` and `PKP_EN57A-OR`,
  MaSzyna's `pkp/en57_v2`; the 14 OR and 6 MaSzyna models whose verdict is
  wrong (their doors too, though most are right: `doors --stage` adds them if
  wanted); the service cars (PPS, Energetyka, 304C Pdn, MaSzyna 204V and
  59WS) and freight loads; WM15A and SR51.
