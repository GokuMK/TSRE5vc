# 12 - Aisles as routes (design)

Status: design (2026-11-13), questions answered 2026-11-17; building since
2026-11-18 (the user's go, on branch `feature/aisle-routes`). The user's
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
  The rise a join may have: up to 1.2 m (a car's raised end); more is not a
  join (a deck above).
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
  the way). A level change's gap: up to `JoinGap` + `StairsRun` (2) times
  its rise (its stairs' run; ED160's entrances are 0.6 m below its
  saloons); one floor within `LevelStep` (0.08 m).
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
  floor faces never taken as cushions, its own aisle line where it has
  benches either side (else the main floor's).
- A level without seats is no aisle (an entrance's floor a step below the
  saloon, EN57's at 0.98 m, ED160's at 0.6 m): people step in onto the main
  floor's aisle as on a car of one floor. So a car of one floor keeps its
  one aisle and its seats as before.
- Against set 2's finder on the 377 models of the reviewed layouts: 255 the
  same (seats and aisle), 122 with seats on another level (76 OR_PSK_NEW,
  46 MaSzyna), none changed otherwise. Those gained are mostly real: SA134
  B 32 -> 62 (the low-floor saloon task 11 called harmless to miss), EN57
  857's end saloons 76 -> 92, MaSzyna's Impuls and Elf interiors +8 to +24,
  SA136/SA139; some will be junk (flip seats by doors, a raised floor over
  a bogie). The "wrong" two-level cars of task 11: ED78 D 8 -> 48 (the low
  saloon), Impuls A 8 -> 49, D 31, Elf A 15 on the raised floor by the cab
  (with junk on the gangway end's raised floor over the bogie).
- Live (a scratch game root, the staged Impuls A and D): PRUDNIK 0844_KMAL
  at Myslowice 09:04, both cars logged with A1 0.81 m and A2 1.06 m joined
  at 2.75 - 3.00 m; the stop ran as before (36 off, all waiting on).

### Left
- Set 3: the 122 models and the two-level cars written again (their seats
  renumbered: the set 2 drops carried over by place, as set 2 did), their
  plans reviewed, staged and archived as sets 1 and 2; the cars of one
  floor need no new set (their .sd read as before).
- Rule 4 (an aisle per saloon, ED160 b's 2+1 seating swapping sides): not
  built yet.

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
