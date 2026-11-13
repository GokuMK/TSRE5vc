# Task 06 - Boarding and getting off

Status: built on `feature/humans`, 2026-11-12, with task 05
(05-people-on-platforms.md). Waits for the user's tests.

## Goal
When a train stops, people get off and the waiting people board. The first
version changes nothing in the simulation.

## The user's decisions (2026-11-12)
- **No vanishing in the doorway**: people walk in through the door and on
  into the car up to the track's middle, and are gone there, where the
  car's body hides them.
- **The train's floor at the door's height**: people walk in at the door's
  threshold. Not always true, enough for this step.

## What was built (in `Humans::StationPeople`)
- **Approach**: as its train comes within 500 m of the stop (or stands
  there), seven in ten of a group step up to just behind the edge zone,
  within the stretch the train will stand at: Open Rails stops trains
  centred on the platform, so the platform's middle, a train's length long.
  People beyond it walk into it (a little quicker from far).
- **Doors** (`Humans::CarDoors`, `src/tsre/humans/CarDoors.h`): a car's
  shape read once on a worker for its door parts by Open Rails' names
  (`DOOR_A`..`DOOR_C` one side, `DOOR_D`..`DOOR_F` the other, as
  CarAnimation groups them; their children too), their triangles at rest
  split into doors where along the car they leave a gap (a leaf is one
  triangle's span: a flat leaf's corners lie apart); 0.4 to 4.5 m wide.
  Each door: its group, its middle along the car, its width, its outer face,
  its threshold's height. Without door parts, a carriage (and each car of a
  multiple unit: a passenger train without carriages) gets a door near each
  end, level with the platform. MSTS shapes only for now.
- **Where the doors are**: each car as last drawn (new `Eng::lastDrawn`: the
  body's matrix as placed, its tile, when). A door counts when the
  platform's walk area lies in front of it (within 2.5 m out, the platform
  up to 1.6 m below or above the threshold) on the stop's platform side
  (a train between two platforms has doors toward both).
- **Open or closed**: the car's door group as the simulation set it
  (`Eng::isAnimated`: Open Rails' rule for AI trains, 4 s after arriving,
  10 s before leaving; the driver's for a driven train); a door near a
  carriage's end opens with either side.
- **Waiting by the doors**: once the train stands and its doors are known,
  its people walk (a little quicker) to wait beside the nearest door, either
  side of it in turn and out from the edge; the busier doors count 2 m
  farther.
- **Getting off**: from each open door, one every 1.1 s or so: out of the
  car's middle over the track, through the threshold, onto the platform,
  then away along it to near one of its ends (mostly the farther), gone as
  soon as the camera does not see them. As many as the simulation counts
  (below), shared over the doors.
- **Getting on**: at each open door once the last one off has had 2.5 s
  (1 s after opening when none get off), one person every 0.7 s, the
  nearest of those waiting by it first: to the door's front, then (within
  1.2 m, or held up within 4.5 m) straight through the threshold and on to
  the car's middle, at the threshold's height, gone there.
- **Closing and leaving**: a door closing sends those more than 4 m from it
  back to waiting; those nearer still get in. The train leaving: those in
  its doors are in, the rest walk away and are gone where nobody sees them.

## Checks
- Suite `station-people`: an EN57's two doors of two leaves each found at
  their places, 1.3 m wide, the floor at 1.14 m; pieces too narrow or too
  wide dropped.
- Routes (`renderer-capture` stepped 0.25 s a frame, frames saved, and the
  log):

| Route, run | Train, stop | Doors | What happened |
|---|---|---|---|
| EUROPE1, `lclsrvce.act` from 09:00:03 | the player's train (MSTS stock coaches, no door parts) at Carlisle, its first stop; 25 s standing, doors open 11 s | 20 end doors on the platform side | none got off; 21 of 25 boarded, 4 too far from a door as it closed walked away |
| PRUDNIK, `PSK_2026.timetable_or` from 05:38:00 | 0518_R46403, EN57AL (door parts), Raszowa, early (05:38:10), until 05:39:30 | 6 of its 12 doors on the platform side, floor 0.66 m above the platform | 3 got off; all 20 of its people boarded; the other train's 12 kept waiting |

From the platform at Raszowa: the EN57's folding doors open, people step
into the doorway at the floor's height and the doors close behind the last
of them; one is seen through a window walking in. At Carlisle people walk
into the coaches' end doors and the bodies hide them.

## Left
- Short stops: the simulation now holds an AI train for its people (below),
  at most 30 s; people far from a door may still miss it, and wait for the
  next train.
- Doors without parts are level with the platform: inside a coach whose
  floor is higher the walkers are lower than the floor (hidden by the body).
- The step from the platform up to a high floor is walked as a slope.
- Stops away from the platform's middle (exit signals, `$closeupsignal`):
  people approach the middle and walk farther once the doors are known.
- glTF, MaSzyna and other shapes without readable door parts get the end
  doors.
- Those walking away stay while the camera sees them (no exits until task
  07).

## Boarding time in the simulation (built 2026-11-12)
The user, 2026-11-12: sooner than task 10; a missed passenger is fine, but
what happens to him must be handled, and missed passengers counted (an
activity may judge the player by them later); the simulation may wait some
30 s for boarding, then goes anyway; a driven train's doors are the
driver's. Then, on the design below: go ("if that is the right thing").

Why in the simulation, not a message from the people: people exist only at
stations within 1.5 km of the camera (and never while a run is stepped to a
time without drawing); they move at most 0.5 s of simulation time a drawn
frame, so at 10x or on slow hardware they would hold trains longer; and a
held AI train holds signals and crossings the player meets. The counts an
activity judges by must not change with where one looks or the frame rate.

- **The input** (`Sim::FillPassengers`, the controller's input after the
  work orders): each stop's people waiting for the train (the platform's
  passengers waiting; none at its last stop, none for a train without
  passenger doors), getting off (repeatable from the train and the stop:
  none at its first stop where it starts, 3 to 8 a passenger car at its
  last, 0 to 3 between), and the platform's key (its lower track item).
  `Sim::PassengerDoors`: two a side a car that carries people (a carriage,
  or `PassengerCapacity`), else every car of a passenger train of two cars
  or more (a multiple unit).
- **Arriving** (`SimulationCore::passengersArrive`; an AI train, a driven
  one, one starting at its first station): those waiting, with those
  earlier trains left behind at that platform (not at the train's last
  stop), and those getting off. An AI train stands until they are through:
  doors open 4 s after arriving, a 2 s margin to reach them (10 s at first;
  the user chose 2 s on the measures below), 2.5 s a person
  a door, doors closed 10 s before leaving; at most 30 s past the time it
  would have left anyway.
- **Leaving** (`passengersLeave`): the doors' time (an AI train's by Open
  Rails' rule as above; a driven train's while its driver kept either side
  open, the cab's `doorsLeft`/`doorsRight`), less the margin, at 2.5 s a
  person a door: those getting off first, then those getting on; the rest
  are left behind at the platform and wait for the next train stopping
  there (any train, until task 10's preferences).
- **Told**: `TrainState::atStation` (the stop it stands at, an AI or a
  driven train), `TrainState::passengers` (the stop, waiting, getting off,
  got off, got on, left behind, and totals since the run began, for judging
  the player later), in every frame; an event `Passengers` (the editor's
  log: "at X: N got on, M left behind").
- **The people** follow it: a group is as many as the stop's waiting; those
  getting off at a standing train are its count, shared over its doors; its
  doors close when the simulation's time says; as it leaves, as many of its
  people as it left behind stay (back from the edge) and join the next
  train's group at their platform side (given up after half an hour without
  one), the rest walk away.

Developer switch: `TSRE_PASSENGERS_SCALE=<factor>` multiplies every
platform's figure in the run's input (to try crowds and trains left
full); `TSRE_PEOPLE_LOG=1` tells, as each train leaves a stop, the
simulation's counts beside the people drawn.

Checks: suite `simulation-core` (+2): a train held 30 s past its time for 40
waiting and 10 getting off at its two doors, the people through its doors
in that time counted on, the rest left behind and found waiting by the next
train there with its own 5; a train without passenger doors not held, no
one counted.

The people drawn follow its pace: an AI train's people go in at its rate
(SecondsAPerson a door of its doors), no more than it counts on by the time
it leaves (its departure known); a driven train's no more than its doors'
time so far allows. Waiting people leave room (40) under a station's 250
for those getting off.

On routes (`renderer-capture`, `TSRE_PEOPLE_LOG`):
- EUROPE1 `lclsrvce.act`, Carlisle, the player's train at its first stop:
  25 waiting, 18 doors; held 1 s; the simulation counts all 25 on, 21 drawn
  got in, the other 4 walked away.
- PRUDNIK timetable, Raszowa, the EN57AL with `TSRE_PASSENGERS_SCALE=15`
  (300 waiting at its 6 doors): held 30 s past 05:39:30; the simulation
  counts 210 on, 90 left behind, 7 off; drawn (210 made, the station's cap):
  7 got off, 192 got in, 18 stayed for the next train.
- Cost there, with some 200 people walking at once: 5 to 21 ms a frame for
  the people (Detour's crowd most of it) at 0.5 s of simulation a frame (5
  crowd steps); in real time one step a frame, about a fifth of that. To
  measure on the user's hardware; cheaper avoidance for people far off and
  standing people kept out of avoidance are the first ways down.

Left: the platform's side for a driven train's doors (either side counts
now); door parts as doors' number (two a car now); people giving up on a
platform in the simulation (they wait there for good now; the people drawn
walk away after half an hour without a train).

## Does it matter? (measured 2026-11-12)
The user: "run our simulation test runs on MSTS activities and Prudnik and see
if this new sim addition matters at all." Suite `simulation-boarding`
(--game-root, --route): every activity and timetable set run to its end
twice, without the stops' people and with them (`FillPassengers`), stops'
departures and trains' ends compared. `TSRE_BOARDING_MARGIN=<s>` (a
developer's switch) stands in for the margin.

| Route | 10 s margin (as built): stops left later, mean / most | 2 s margin: stops left later, mean / most | 2 s: trains ended later, most |
|---|---|---|---|
| EUROPE1 (10 runs) | 340 of 417, 42.5 / 150 s | 190 of 417, 6.1 / 26 s | 19 of 48, 26 s |
| EUROPE2 (9) | 45 of 174, 81.6 / 234 s | 38 of 174, 13.3 / 32 s | 5 of 21, 32 s |
| USA1 (9) | 91 of 94, 17.7 / 57 s | 57 of 94, 3.6 / 9 s | 30 of 46, 9 s |
| USA2 (16) | none | none | none |
| JAPAN1 (8) | 1002 of 1117, 17.6 / 61 s | 136 of 1117, 2.0 / 2.5 s | 7 of 50, 3 s |
| JAPAN2 (8) | 18 of 62, 39.5 / 118 s | 15 of 62, 21.4 / 54 s | 6 of 16, 54 s |
| PRUDNIK (timetable) | 38 of 1794, 7.8 / 30 s | 12 of 1794, 6.3 / 24 s | 2 of 296, 24 s |

As built it matters: before anyone boards the rule takes 24 s (doors open
after 4 s, 10 s to reach them, closed 10 s before leaving), more than Open
Rails' least stand at a stop (20 s, a late train's), so nearly every stop
with people is held and the holds add up down the line; Open Rails' own
boarding time (10 s a waiting passenger, over the cars) already stands for
the people. With 2 s to reach the doors (people already wait beside them)
the holds are a few seconds, now and then up to 30 s at a crowded stop.
PRUDNIK's timetable trains have long booked stands and hardly change.
The user, 2026-11-12: 2 s, and a setting to turn the holds off "so that
people using TSRE as an ORTS editor can simulate ORTS-equivalent times":
`core.simulation.boardingHolds` (Advanced > Simulation, "Trains wait for
boarding passengers", on by default, read as a run starts; the run's
`SimulationInput::boardingHolds`). Off, the people are still counted (and
drawn), and every stop is left at Open Rails' time: the suite's check, a
third run per activity with the holds off, every departure the same as
without people on all seven routes (EUROPE1, EUROPE2, USA1, USA2, JAPAN1,
JAPAN2: 60 runs; PRUDNIK's timetable); `simulation-core` +1 (92).

## Steps into the car (built 2026-11-12)
The user: go, "can one clip apply to two types of steps: steep ones and more
normal ones?" No: a train's steep steps (0.3-0.4 m high, shallow, a hand
pulling on the rail, the body leaning in) and ordinary stairs (0.17 m by
0.29 m, much like walking) move the body differently; within a family small
differences are scaled. Train steps now; stairs (looping clips for
footbridges) with task 07.

- **humans-gen** `scripts/steps.py` (called by `build_humans.py`, after the
  retargeted clips, on each human's armature with the shared joint frames):
  `steps_up_1..3`, `steps_down_1..3`, steps 0.35 m high and 0.25 m deep;
  the legs by IK onto each tread (the right foot leading, the other joining
  on the floor), feet flat, the pelvis following them up (leaning in going
  up, upright going down) on the idle clip's upper body and its own pelvis
  turn, the right hand on a grab rail beside the door; baked into plain
  rotations and the pelvis's translation, keyed every frame. The clips carry
  their travel. Both packs remade (the shared clips file 0.81 MB with them;
  the basic pack 2.3 MB).
- **TSRE**: `SkeletalAnimation::clipTravel` (how far a clip carries the top
  joint); `Crowd::clipNamed`, `clipLength`, `clipTravelOf` (turned and at
  the person's size), `steerClip` (an owner's clip at its time; far off,
  walk frames); clips named `steps_*`/`stairs_*` are never a person's idle
  or walk. At a door (`StationPeople::doorScript`) the floor's height above
  the platform picks the clip (0.35 m a step, 0 to 3; none: walked as
  before): boarding, the person walks to where the clip ends at the
  threshold, plays it in place (the clip carries the body, the height's
  difference made up over it), then walks to the car's middle; getting off,
  from the threshold down. A door without door parts takes the train's
  floor 1.1 m above the rail (a coach's).
- Checked: the EN57AL at Raszowa (floor 0.66 m above the platform, 2
  steps): a woman climbs into the doorway, a hand to the rail, and walks in
  at the floor's height; suite gltf-skin's humans now 13 clips.

## MaSzyna cars' doors (2026-11-12)
The user: MaSzyna's .mmd/.e3d cars before the .sd doors block ("those
shapes are better quality"); first see whether they open their doors at
stops.

- **They do**: CarAnimation rigs a MaSzyna car's doors from its .mmd (the
  door prefix and number, odd ones its left, even its right; shift, rotate,
  fold or plug as its .fiz says; steps too), on the same left and right
  groups the simulation opens at a stop. Seen on MaSzyna's EN57 (its own
  set: 6baiv_led7, 6bsiv_led7, 6bbiv_led7, livery EN57-1368'19) standing
  in for 0518_R46403 at Raszowa: its sliding doors open as it stands.
  (A scratch game root: OR_PSK_NEW linked, one consist and a copy of the
  timetable with that train's consist changed.)
- **Built**: `Humans::CarDoors` reads a MaSzyna model's doors: the parts
  named the .mmd's `animdoorprefix` and a number (its door count, else 8),
  with the parts under them, read fresh on a worker (the shared model gives
  its vertices to the renderer), placed by the parts' matrices; the same
  split into doors. The EN57: two doors a side a car, 1.5 m wide, the floor
  0.63 m above Raszowa's platform (2 steps): people step into the doorway
  and walk in, where the end doors were guessed before.
- Suite `station-people` +1 (13, with TSRE_MASZYNA_ROOT: the EN57's middle
  car).

## Later (the user's questions, 2026-11-12)
- **Up to 1000 people a station** (8 platforms with traffic, 100 a
  platform): 250 was a guess at Detour's cost (every agent stepped each
  frame). Only people who move need an agent: those standing give it back
  (idle, no steering), those who must walk take one from a pool; the cost
  then follows those walking at once. Walkers would not avoid those
  standing (keep standing places off the ways to the doors). With it, the
  people's step off the render thread (now inside the scene's gathering):
  stepped on a worker between frames, the render thread reading places.
- **Doors without door parts are a guess** (both ends, both sides, the
  floor 1.1 m up): right for coaches with end vestibules, wrong for
  compartment coaches (doors along the side). A doors block in the car
  shape's `.sd` (where its doors are, its floor's height), as TSRE extends
  .sd files, would let content say so.

## Review (the user, 2026-11-13): getting off
A person stepping out of the older EN57 had the head above the door. The
step clips down are made with their origin at the bottom of the steps:
`steps_down_2` begins the feet 0.70 m above it (`steps_up_*` begin on it).
The script held the origin at the car's floor as the clip began, so people
getting off rose by the steps' height at the threshold (head in the roof)
and dropped to the platform as the clip ended. Now
`SkeletalAnimation::clipStartLift` measures how high a clip begins the feet
(the skin's lowest end joint at its start less in the rest pose; the rig's
root joint stays on the ground) and the script's origin goes from the top's
height less that lift to the bottom's, for both ways; humans-crowd +1.
Checked on PKP_EN57-1310 at Raszowa (heads inside the door, a head below
its top).

## Review (the user, 2026-11-13): knees bent backwards on the steps
Climbing the EN57's steps, people's knees bent the wrong way, "like the leg
is rotated 180 degrees". Measured on the man's clips (the knee against the
line from hip to ankle, ahead of it as people walk): steps_up_2 and _3 and
steps_down_2 and _3 had it behind, up to 10 cm going up and 29 cm going
down; the one-step clips never. humans-gen's `steps.py` pointed each knee by
an IK pole target fixed 0.6 m ahead of where the knee stood at the start:
after a step or two the body had passed it, and the knees bent towards it,
backwards. And the pelvis rose with the higher foot, the swinging one too,
so the standing leg was stretched past its length. Now the poles go with
the pelvis, and the pelvis rises no higher than both feet let it (each leg
at most 96 % of its length from hip to ankle): no knee behind the line in
any of the six clips, the knees 7 to 11 cm ahead at their straightest.
Only the shared clips file changed in the packs (the human files carry no
clips: everyone plays the man's steps, the same rotations on every body).
