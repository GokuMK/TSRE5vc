# 12 - Aisles as routes (design)

Status: design (2026-11-13), questions answered 2026-11-17; built
2026-11-18 (the user's go, on branch `feature/aisle-routes`), set 3 staged;
merged into main 88663deb (2026-11-18). The user's
additions with the go: the seats tool generates the aisles (floor levels
in this task, not a later step); rule 4 of task 11 (an aisle per saloon) in
it or not, decided during the work.

## The user's brief (2026-11-13)
- A car's aisle spans one line end to end, while many cars have obstacles
  on the way (task 11's mapping: aisles through cabs, toilets, partitions,
  armchairs): it must be possible to trim it.
- Aisles that connect: an aisle has its floor's height (y) and people take
  that height as their floor, so following the next aisle takes them onto
  another level without routing through the shape.
- First version without a step clip at a level change (the human animation
  team's work).

## How it works now
- `ESD_TSRESeats` may hold several `Aisle ( x y z0 z1 )` lines (a glTF's
  `"aisles"`); `ShapeSeats` reads them all and `StationPeople` merges a
  car's models' aisles, each moved by its model's offset.
- `StationPeople` uses them in three places, always through `aisleAt(seats,
  z)`, the aisle nearest a place along the car:
  - boarding (`seatPerson`): a person who stepped in starts on the door's
    aisle at the door's z;
  - to the seat (`sitPlan`): from there straight to the seat's aisle at the
    same z, along it to the seat's row, sideways to stand in front of it;
  - getting off (standing up): to the door's aisle at the seat's row, along
    it to the queue's place by the door.
  Cars filled as they come into sight put people straight on their seats.
- So an aisle's ends only choose the aisle: nobody is kept within them, a
  change of aisle is a straight line across the car wherever the person
  is, and a seat is picked among the six free ones nearest the door along
  the car, whether its aisle can be reached or not. Passages are read but
  not used.
- The finder writes one aisle a car: the floor's cells along its line, gaps
  up to 0.5 m bridged, from the car's middle outwards; it runs through
  partitions (passages), toilets' doors, into cabs.

## Proposal

### 1. Aisles are pieces, joined where they meet
The line stays as it is; a car may have as many as it needs, each a
straight piece along the car at its own x and floor height y.

- **Joins are found, not written** (TSRE's rule: computed at load where it
  can be): two aisles join where their ranges along the car overlap or
  leave at most `JoinGap` (0.3 m) between them. The join is at the middle
  of that overlap (or gap); a person crosses there, straight from one line
  to the other.
- **A level change is a join** between aisles of different y: the leg
  across it rises or falls (the walking position is interpolated in 3D as
  now: in this version people glide up; later the join knows its rise and
  picks a step clip, as `doorScript` picks `steps_up_N` at a door).
  The rise a join may have: up to 1.5 m (a car's raised end; a double-deck
  car's stairs from its entrance up to its upper deck, the Bhp's 1.28 m:
  1.2 m before the user's report of 2026-11-18); more is not a join (one
  deck above another).
- Examples:
  - a trimmed aisle: one piece ending at the last seat row before the cab;
  - an obstacle on the line (a toilet block, a column): three pieces, the
    middle one beside it, joined at their overlaps;
  - 2+1 seating swapping sides at the vestibule (ED160 b): one piece a
    saloon at its own x, a short one through the vestibule between them;
  - a low-floor middle and raised ends (ED78, Impuls, Elf): a piece on each
    floor, joined where the steps are.

### 2. Routing in the car
- A car's aisles and joins as a small graph (a handful of aisles), made
  once when its seats are merged (`carSeatsOf`).
- Each seat belongs to one aisle: among those whose range holds its z
  (with the finder's 0.5 m), the one with the nearest floor height, then
  the nearest in x. Each door likewise (its threshold's z and height).
- A rider's way: from the door across to the door's aisle, along it to the
  join toward the seat's aisle (breadth-first over the graph), across,
  along the next... to the seat's row, sideways to stand in front of the
  seat. Getting off: the same way back, to the queue on the door's aisle
  (the queue kept within that aisle's range, turning onto the next aisle
  if longer).
- Seat choice: among the seats whose aisle can be reached from the door's,
  the six nearest by the way's length (instead of along the car). A car
  whose aisles do not all connect gives each door the seats it reaches.
- Getting off: the riders nearest their door by the way's length.
- One aisle (every `.sd` written so far): routes as now, nothing changes.

### 3. Trimming and editing (the tool)
- Aisles get numbers as seats and doors (`Aisle 1 ( x y z0 z1 )`, the
  label read as its number; an unnumbered one still read) and the plans
  show them (A1, A2...) with their joins.
- A command to set a car's aisles, replacing the block's Aisle lines with
  those given, its other lines kept (as `doors` does with Door lines):
  `car_seats.py aisles <model> "x y z0 z1" ... --root ... [--stage ...]`.
  Trimming one is giving it again with its new ends.
- The finder (a later step, after the game routes): each aisle's ends at
  the last seat row or door beyond which there is nothing, and not into a
  cab (task 11's end room); a piece per floor level once the finder finds
  benches per level (task 11's rule 5).

### 4. In TSRE
- `ShapeSeats`: aisle numbers; `ShapeSeats::joins(aisles)` and a route
  (`from aisle, z` to `aisle, z`, the points between) as plain functions,
  tested on their own.
- `StationPeople`: `sitPlan`, `seatPerson`, standing up and the choice of
  seats and of those getting off take the route; a car's aisles and joins
  logged once with `TSRE_PEOPLE_LOG=1`.
- Shape Viewer, 3D View > Seats: the joins drawn between their aisles.
- Checks (station-people): two overlapping aisles join at their overlap's
  middle; aisles at different heights join and the leg rises; a route over
  three aisles gives the expected points; seats on an aisle the door's
  cannot reach are never chosen; a car with one aisle routes as before.

## Not in this design
- Step clips at a level change (the human animation team).
- Obstacles beside the line: people walk the aisle's line; nothing else in
  the car is avoided.
- Compartment cars (a corridor and compartments across it): an aisle runs
  along the car only; compartments would need pieces across it. Later.

## Built (2026-11-18)
- `ShapeSeats`: an Aisle's label is its number (a glTF's aisles numbered by
  their place); `joins()`, `aisleFor()` / `aisleOfSeat()`, `connect()` (the
  joins and each seat's aisle, after reading, merging and `drawn()`),
  `route()` (the shortest way over the joins, Dijkstra over the start, the
  end and the joins' ends), `wayStart()` / `wayShort()` (a queue's place
  along a way), `seatsByWay()` (the free seats a place reaches, nearest by
  the way). A level change's gap: up to `JoinGap` + `StairsRun` (5) times
  its rise (its stairs' run; ED160's entrances are 0.6 m below its
  saloons); one floor within `LevelStep` (0.08 m). Replaced on 2026-11-18
  by stairs as aisles (below).
- `StationPeople`: the seats' cache connects each car's aisles; a door's
  aisle by its threshold's height and place; the walk to a seat, to the
  door and the queue's places follow the way; seats chosen by the way among
  those the door reaches; getting off, the riders nearest by the way of
  those who reach the door; those left at the end of a stand back to the
  nearest seat they reach. A car's aisles and joins logged once a model
  (`TSRE_PEOPLE_LOG=1`).
- Shape Viewer, 3D View > Seats: the joins (orange) between their aisles.
- `car_seats.py`: Aisle lines numbered (`Aisle 1 ( ... )`); `aisles` (a
  model's Aisle lines replaced, its other lines kept); the plans show each
  aisle's number and floor height (A1 0.81) and the joins (TSRE's rule);
  the finder's floor levels (below).
- Checks (station-people): numbered aisles read; overlapping aisles join at
  the overlap's middle, 0.4 m apart on one floor not; a level change joins
  at its stairs' ends and the way rises over it, a deck 2.5 m above does
  not join; a way over three aisles; a queue's place 1.1 m short; one aisle
  as before; each seat's aisle by its floor; the seats a door reaches by
  the way (an unreachable deck's never, a taken one never).

### The finder's floor levels (rule 5)
- The main floor (the most area) found as before; then the floors covering
  the aisle's line itself (seen from above, each 0.25 m along it) by
  height, one floor within 0.08 m. A level: 0.15 to 1.2 m from the main
  floor, covering the line 1.5 m or more; its stretches (1 m or more, gaps
  up to 0.75 m bridged) are its aisles, not into a cab (the finder's cab
  cut). Each level's seats: its cushions over its stretches, other levels'
  floor faces never taken as cushions, nor a cushion nearer a cushion's
  usual height (0.45 m) over another floor (that floor's: a low entrance's
  "seats" were the saloon's benches by it), nor a seat already found; its
  own aisle line where it has benches either side (else the main floor's).
- The main floor's other stretches (a saloon past a low middle, SA103's):
  their seats too.
- A level without seats is no aisle (an entrance's floor a step below the
  saloon, EN57's at 0.98 m, ED160's at 0.6 m): people step in onto the main
  floor's aisle as on a car of one floor. So a car of one floor keeps its
  one aisle and its seats as before.
- Set 3 (below) is these levels' seats, reviewed. (A first count here said
  EN57 857's end saloons gained 16 seats: they were the saloon's own seats
  found a second time from the entrance's floor, fixed by the cushion
  rule above.)
- Level changes join over stairs or a ramp up to `StairsRun` 5 times their
  rise (2 at first): the Elf's and Impuls' raised ends lie a vestibule's
  ramp beyond their low floor (EN76: 0.43 m over 2 m), unreachable with 2.
  (Until 2026-11-18: now written as stairs, below.)
- Live (a scratch game root, the staged Impuls A and D): PRUDNIK 0844_KMAL
  at Myslowice 09:04, both cars logged with A1 0.81 m and A2 1.06 m joined
  at 2.75 - 3.00 m; the stop ran as before (36 off, all waiting on).

### An aisle per saloon (rule 4 of task 11)
Decided during the work (the user left it to it): it fits here, as the
finder's half of several aisles a car.
- The main floor's benches grouped into saloons along the car (more than
  1 m apart: another saloon); a saloon's aisle the widest gap across the
  car between its cushions (0.4 m or more). Saloons whose aisles lie more
  than 0.25 m apart get an aisle each, neighbours overlapping by 0.25 m
  either side of the middle of the gap between them (the vestibule: TSRE
  joins them there, decision 1); a saloon without a gap of its own (a
  cab's one seat) takes its neighbour's; the places kept off the aisle's
  line (rule 8) and the partitions go by the saloon's aisle. Only where
  the car's one line was found before (a car the finder gave up on stays
  so: MaSzyna's compartment cars would have got empty aisles) and only on
  the main floor (a level's stretches are short: one line each).
- Against the level finder on every passenger model of the scans (1181):
  1164 the same; split: ED160 b (-0.33 / +0.33: its second saloon's
  armchairs off the aisle, 46 -> 48 seats), 101A 1st class (+-0.29: its
  sides swap at the middle), ED72 ra and EN57-1752 rb (the cab end's
  saloon at the middle, 0.29 from the others), SA136 A, MaSzyna's EN57
  6bbii/6bbiii (the 2+1 middle saloon at -0.27), ET21 (a loco, no seats).
- Left as it was: the three places on each pair of the ED160 b's armchairs
  (their cushions touch: one piece, rule 3 does not part it; `drop` in the
  review).

### Set 3 (2026-11-18)
The seats sets of task 11 written again where this task's finder changes
them, as set 2 was (`~/humans-11`, never the content: `stage3-<set>/`,
`stage3-plan-<set>.json`, `README3-<set>.md`, the archives
`OR_PSK_NEW-sd-seats-map-20261118.zip` and `maszyna-sd-seats-map-20261118.zip`
with their `-backup.zip`; plans of the review in the session's scratch).
- Every model set 2 wrote found again and matched by place to the seats set
  2 wrote and to the places it took out: 271 the same (set 2's file kept
  as it is), 13 with the same seats and new aisles (rule 4), 102 with seats
  set 2 lacks; plus the "wrong" two-level and 2+1 cars of task 11 (ED78 A
  and D, Impuls A and D, ED160 b) and the Elf A cars (2 seats before).
- Reviewed on plans, one a layout (the seats set 2 lacks marked): real
  seats on raised ends and low-floor saloons (SA103, SA133, SA134, SA136,
  SA139, the Elf, KW EN76 D, SN84, MaSzyna's Impuls and Elf interiors and
  SA134); taken out: entrance steps (ED160 d/g, DB Bpmz), a ledge along a
  wall (EN57-1291/787 middle cars, SA134-011 A), a cab's points (Elf A/D),
  vestibule partitions (ED78 D, Impuls A/D), the middle of three places on
  each pair of the ED160 b's armchairs and its end wall's two, places
  facing a panel (SA136 A/C), one of two back to back (MaSzyna SA134 A).
  Where all of a car's new seats were junk, set 2's file stays (ED160 d/g,
  EN57-1291/787 s, DB Bpmz, 159A B9, MaSzyna 14xA 141a-20, Z2 restaurant).
- Staged: OR_PSK_NEW 419 `.sd` (set 2's 411 and the 8 cars that had none;
  63 written again, 2714 seats where set 2 had 1602), MaSzyna 139 (44
  written again, 1849 seats for 1380). Checked: each written car's seats
  as planned, its doors as in set 2, every other file byte for byte set 2's.
- Live (the scratch root with set 3): see below.

### Left
- Step clips at a level change: the stairs clips (2026-11-18, below).
- People waiting beyond a train's end on a long platform (Ząbkowice: 35 and
  62 m from the nearest door) are still walking when the doors close: 2 of
  7 left on the platform while the simulation counts them in. Not this
  task's (where people wait, task 05/06); for the user to decide. Fixed
  since (main 84f8dcdb, `06-boarding.md`'s review of 2026-11-18).
- Double-deck cars' lower decks: built (2026-11-18, below).
- The Görlitz cars' upper decks (left out: the lower deck as a start).

### Double-deck cars (the user's report, 2026-11-18)
PRUDNIK's 1707 at Nysa (an SU42 and a Bhp set: two halves and the joint
between them) had no seats: set 3 gave its cars doors only, the mapping
report calling the Bhp's interior "thin panels, no cushions". Wrong (the
user's review): their cushions sit 0.4 m above each deck's floor. The
finder took the doors' floor between the decks (1.12 m) for the car's (the
decks, 0.36 and 2.4 m, lie outside its 0.6 to 1.8 m) and found no aisle
there.
- The user's decision: the upper deck only. The lower deck's way in is
  down side stairs to a landing beside a bench standing on the aisle's line
  under the stairs up; the aisles as they are would join the entrance
  straight to the lower deck's aisle, through that bench (a rule against
  joins through seats, and the side stairs found, would be needed).
- Built (branch `feature/double-deck`):
  - The finder's `upper_deck()`: the floor 1.8 to 3.2 m above the rail and
    at least 1 m over the main floor with the most area, its seats and
    aisle as on any floor; taken where it has more seats (8 at least) than
    the main floor with its levels (MaSzyna's Bhp interiors found their
    lower deck as a level of the doors' floor, its way through that bench).
    The doors' floor along the deck's aisle line beyond each end of the
    deck: an entrance aisle, ending at the foot of its stairs up (the
    treads over the line between the two floors, running on from the
    deck's end).
  - TSRE: `JoinRise` 1.2 to 1.5 m (the Bhp's stairs rise 1.28 m; no set 3
    car has aisles 1.1 to 1.6 m apart in height); a station-people check
    (an entrance and its deck 1.28 m up joined, two decks 2 m apart not).
    Replaced the same day by stairs as aisles (below).
  - TSRE: no doors made up for a carriage shorter than 5 m (the Bhp's 0.2 m
    joint got two, people used them; OR_PSK_NEW has 6 such: three Bhp
    joints, SA134's two `_pp`, an EMU's coupler), nor does one make its
    train no multiple unit (`06-boarding.md`).
- Found in the whole content (OR_PSK_NEW and MaSzyna scanned): 82 OR Bhp
  models (56 `PKP_Bipa_Bhp_VD-OR`: 48 seats, 58 on the `Bipa*-IIIc1`; 26
  `PKP_Bhp-OR`: 44, the report's "no interior modelled" wrong there too)
  and MaSzyna's 5 `bhp_v1/low_poly_int` interiors (40). The Görlitz
  double-deck cars (`PKP_Gorlitz88` 60 seats on the upper deck,
  `PKP_Gorlitz88-OR` 64 against 8 on its end's level, MaSzyna's
  `int_bdhpumn_*` and `int_g76_mod_294` 60) are left out: their doors are
  on the lower deck (thresholds 0.49 to 0.51 m, 6 m from the middle, under
  the upper deck), below the finder's floors (0.6 to 1.8 m); the ends over
  the bogies are a middle level (1.2 to 1.31 m), the upper deck 2.55 to
  2.63 m. The user (2026-11-18): the modern layout (doors on the lower
  deck); the finder may take the lower deck and leave the upper one out;
  a middle level often has seats too. Other
  decks found (an ET42's roof, EN57 racks, a freight car) have fewer than 8
  seats: none.
- Set 4 (staged in `~/humans-11`, never the content: `stage4-<set>/`,
  `stage4-<set>.log`, `README4-<set>.md`, `*-sd-seats-map-20261118-4.zip`
  with their `-backup.zip`): set 3 with the Bhp cars. OR_PSK_NEW 445 `.sd`
  (56 written again with their seats, doors as in set 3; 26 new), MaSzyna
  144 (5 new); every other file byte for byte set 3's.
- Live (Nysa, 1707, the scratch root with its two cars' new files): TSRE
  joins each entrance to the deck over its stairs; 4 doors on the platform
  (6 before, the joint's two among them); riders sit upstairs (4 of 48 a
  car at the start); 30 of 30 on as the simulation counts, 6 drawn still
  on the platform as it left (1 before). The same 6 with set 3's door-only
  files and the joint fix: the seats do not slow it. Not the doors' pace
  (my first reading, about one person in 20 s a door, was wrong): people
  were still walking to the doors, up to 132 m from the far part of the
  264 m platform (the train at 119 to 173 m of it), and on the software
  renderer (a frame in about 1.2 s) they walk at a third of their pace or
  less, the people's step being at most 0.5 s a frame; a door takes one
  every 0.7 s (`BoardGap`) once they are there. The joint's two doors were
  nearer to some walkers. The simulation counts the joint's 2 doors (TSRE's
  two a car, `PassengerDoors`; Open Rails counts it as a passenger car).

- The Görlitz cars (the user's go, 2026-11-18: the lower deck as a start),
  built on the same branch (eb4f3e06): the finder's `lower_deck()` takes a
  car's lower deck where an upper deck is found and its doors' sills lie
  below 0.6 m (an S file's door parts; a MaSzyna interior's car's doors
  by its `.mmd`'s `animdoorprefix`, as TSRE reads them): the floor along the
  middle line with the most area within 0.3 m of the sills, its aisle on
  over its ramps and the doors' vestibules, its ends' middle levels (over
  the bogies) found each on its own, seats beyond its aisle's ends (the
  stairs' first treads) dropped, cushions from 0.25 m where none from 0.33
  (the Gorlitz88-OR's). The upper deck left out. Their stairs: below.

### Stairs as aisles (the user's design, 2026-11-18)
Merged into main b513bf9f with the double-deck cars (the user's go, 2026-11-19); the stairs clips in the full humans download (`assets/humans.zip`) since 2026-11-19.

The Bhp's lower deck (its way in down side stairs from the entrance, past a
bench across the aisle's line under the stairs up) could not be written
with joins found over a gap: a level change joined any two aisles up to
0.3 m + 5 times their rise apart, so the entrance (1.12 m) and the lower
deck's middle aisle (0.36 m, 1.7 m on) joined straight through the bench,
the shortest way. No one tolerance tells them apart (the Bhp's stairs up:
1.2 m for 1.28 m; the Elf's ramps 2 to 2.5 m for 0.4 m; the shortcut 1.7 m
for 0.76 m). The user: aisles written apart are meant apart; stairs are an
aisle with a different floor at each end; old sets need not be kept; the
finder may be helped by hand where it cannot find them (not needed).

- Format: `Aisle n ( x y z0 z1 y1 )`, the fifth number the floor at the
  second z (written either way along the car); four numbers a level aisle
  as before. glTF: `[x, y, z0, z1, y1]`. `ShapeSeats::Aisle::rise`,
  `yAt()`, `sloped()` (more than `LevelStep`).
- Joins (`ShapeSeats::joins`): only where pieces meet (overlapping, or
  `JoinGap` 0.3 m apart) with their floors at most `OneStep` 0.25 m apart
  there (a step, walked; 1 mm to spare: the Impuls' 0.81 and 1.06); a
  sloped aisle at its ends only, onto stairs only at their ends.
  `JoinRise` and `StairsRun` are gone.
- Stairs are nobody's place: `aisleFor` (a door's aisle, a seat's, a
  rider's) takes level aisles only.
- Walking (`StationPeople`): a leg steeper than 0.45 (rise over run) is
  climbed with `stairs_up` / `stairs_down` at their pace (0.19 m up or down
  a step, at most the walk's speed along), the clip from its start when
  stepping on; a ramp is walked. Without the clips: walked as before.
- The clips (humans-gen `scripts/stairs.py`, as `steps.py`'s door steps:
  IK on foot targets on the treads, the pelvis over the feet, the idle's
  first pose above): loops of two steps of 0.19 m rise and 0.22 m going,
  in place as the walks; up 1.25 s, down 1.0 s. Checked in Blender on
  treads with the body moved along the flight at the clip's pace: the
  feet land on the treads. Built on the man (the shared clips file, 35
  clips); TSRE's basic set's `appdata/0.7/humans/SHAPES/people_clips.glb`
  replaced (the packs' humans carry no clips of their own).
- Finder (`connect_levels()`): every level change between the aisles found
  (more than 0.08 m and not meeting as a step; one floor's stretch ending
  where the other's begins, up to 1 m overlapping; at most 8 times the
  rise apart) written as stairs along its treads (the line through their
  middles, from where it meets the lower floor to where it meets the
  upper; a ramp, or stairs drawn as one sloped face, along its faces) on a
  line a walker is free on: no face 0.3 to 1.2 m over the way within 0.1 m
  of the line (a bench, stairs overhead; not walls, a door's fittings, the
  way's own floor), no seat within 0.42 m; the aisles' own line, else the
  nearest free one beside, pieces on each floor at that line joining it.
  Nothing modelled between floors that meet (SA103's sheer 0.65 m): steps
  assumed over the lower floor at the higher one's edge. A floor less than
  1.8 m under another over it (faces under a raised floor found as a
  level: the Elf's, SA103's) is no aisle, its places out. Not found: not
  joined. `deck_below()`: a double-deck car whose upper deck is taken also
  takes its lower deck (0.4 m or more below the doors' floor), benches
  across its middle line seats of their own (facing the deck's middle),
  its aisle found between them. The `stairs` command: a set's Aisle lines
  written again with their stairs, its seats (reviewed), doors and
  passages kept.
- Checks (station-people, 45): stairs read (either way along the car); a
  step of 0.2 m joins, 0.3 m does not; a raised end 0.9 m along joins only
  by its stairs, the way rising up them; the Bhp's two decks: both stairs
  joined at their ends, no shortcut, the way down by the side stairs, up
  by the middle ones, the bench's seat on the lower deck's aisle.
- Set 5 (staged in `~/humans-11`, never the content: `stage5-<set>/`,
  `stage5-<set>.log`, `README5-<set>.md`, `*-sd-seats-map-20261118-5.zip`
  with their `-backup.zip`; set 5 of the Görlitz work before this design is
  in `obsolete-set5-20261118/`, not to be used). OR_PSK_NEW 449 `.sd`
  (133 written again, 4 new): the 82 Bhp cars with both decks (VD 96 to
  124 seats, `PKP_Bhp-OR` 76 or 80), the 5 Görlitz cars (62, 64), 50 other
  cars with levels with their stairs (their seats, doors and passages as
  set 4's); MaSzyna 150 (47 written again, 6 new): the 5 Bhp interiors (72
  to 80), the 6 Görlitz ones (52 to 68), 42 with their stairs. Every other
  file byte for byte set 4's. Checked: all 205 files with several aisles
  one way under TSRE's joins (506 stairs; 7 steps assumed, SA103's; 14
  cars' hidden floors out, no places with them: set 3's review had taken
  them out).
- Live (Nysa, 1707, 2026-11-19, the scratch root with its two Bhp cars'
  new files; capture on the software renderer): 21 riders on, 16 down the
  side stairs to the lower deck and 5 up to the upper one, one stairs leg
  each; climbing 0.35 m/s along up (1.28 m over 1.45 m), 0.40 m/s down; 30
  of 30 on as the simulation counts, 1 drawn still on the platform as it
  left. (A rider's stairs clip restarted in frames without time, 99 starts
  for 21 riders: fixed, one each.)
- Left: the Görlitz cars' upper decks; plans draw a double-deck car's decks
  over each other; the full humans set's download (`assets/humans`) still
  has the clips file without stairs (the basic set in TSRE has them).

### Found on the way: riders stuck a place short of the door
The user's report (2026-11-18): an ED160 whose people did not get off and
others could not get on. Reproduced at Dabrowa Gornicza Zabkowice (1734_TLK,
17:58): when one rider stepped out, only those already waiting at the door
moved up a place; one still walking down the aisle stopped at its old place,
0.55 m from the door (stepping out wants 0.33 m), so nobody stepped out
again and boarding waited for good. Fixed on `fix/riders-queue` (from main):
each rider keeps its place in the queue, everybody behind moves up when one
goes out (walking or waiting), and a door whose riders are all gone is not
waited for. The same fix here on the routed queue.

## Decisions (the user, 2026-11-17)
1. Joins are found where aisles overlap or nearly meet, not written in the
   `.sd`.
2. Aisles numbered in the `.sd`, and an `aisles` command replacing a car's
   Aisle lines (trimming = giving them again).
3. Seats chosen by the way's length among the seats a door reaches.

## Order with task 11's sets
- The seats sets (task 11, the upgraded finder) keep one aisle a car: the
  game today takes an aisle's ends only to choose the nearest aisle, so
  trimmed or several aisles change nothing until this task is built.
- An aisles pass comes after this task is built: the `aisles` command (or
  the finder's trimming) rewrites only the Aisle lines, so the seats' and
  doors' numbers and the drops made in their review stay as they are.
