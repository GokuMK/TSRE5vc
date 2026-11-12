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
  soon as the camera does not see them. Counts: none at the train's first
  stop where it starts, 2 to 5 a door at its last, 0 to 2 a door between.
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
- Short stops: Open Rails' doors give 11 s at a 25 s stop, and people far
  from a door miss it. The people cannot hold a train; boarding time back
  into the simulation is task 10.
- Doors without parts are level with the platform: inside a coach whose
  floor is higher the walkers are lower than the floor (hidden by the body).
- The step from the platform up to a high floor is walked as a slope.
- Stops away from the platform's middle (exit signals, `$closeupsignal`):
  people approach the middle and walk farther once the doors are known.
- glTF, MaSzyna and other shapes without readable door parts get the end
  doors.
- Those walking away stay while the camera sees them (no exits until task
  07).
