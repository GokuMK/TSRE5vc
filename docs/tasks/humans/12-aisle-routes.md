# 12 - Aisles as routes (design)

Status: design (2026-11-13). Building waits for the user's go.

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
- The finder's floor levels (task 11, rule 5) and its trimming: their own
  step once the game routes over joins.
- Compartment cars (a corridor and compartments across it): an aisle runs
  along the car only; compartments would need pieces across it. Later.

## Questions
1. Joins found where aisles overlap or nearly meet, or written in the `.sd`
   (`Join ( 1 2 z )`)? Recommended: found (no extra data); a written join
   only if a car ever needs one.
2. Aisle numbers in the `.sd`, and an `aisles` command replacing a car's
   Aisle lines (trimming = giving them again)? Recommended: yes.
3. Seat choice by the way's length among the seats a door reaches?
   Recommended: yes.
