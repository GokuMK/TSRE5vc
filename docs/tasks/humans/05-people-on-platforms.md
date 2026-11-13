# Task 05 - People on platforms

Status: built on `feature/humans`, 2026-11-12 (the user: "do 5 and 6"),
with task 06 (06-boarding.md). Checked on EUROPE1 (an activity) and PRUDNIK
(an Open Rails timetable). Waits for the user's tests on this machine's
captures and on the user's hardware.

## Goal
People wait on the platforms for the trains that stop there. The first
version changes nothing in the simulation: a view-side system, like the
displays and the doors.

## The user's decisions
- 2026-11-11: which humans are a player setting (Always, Route defined,
  Never; Always by default); the route's `humans.dat`, else TSRE's default
  humans.
- 2026-11-12: **waiting is counted per train**. Each train stopping at a
  platform has its own waiting people, as many as the platform's passengers
  waiting; two trains due at about the same time bring twice the people.

## What was built
- **`Humans::StationPeople`** (`src/tsre/humans/StationPeople.h`): once a
  frame of the main view, while the simulation shows the route's activity
  (an MSTS activity or an Open Rails timetable set), called from
  `Route::pushRenderOverlays` after the activity's trains. It reads the
  simulation's frames and the trains' cars as drawn, and never writes back.
- **Groups**: one for each stop of each train at a platform of a station
  with a built walk area, within 1.5 km of the camera:
  - how many: `Sim::PassengersWaiting` (the activity's
    `PlatformNumPassengersWaiting`, else the platform item's; the same
    figure Open Rails' boarding time takes, now shared from
    `SimulationInput`); none at a train's last stop (it ends there);
  - when: over the ten minutes before the booked arrival, the last a minute
    before it; a stop without a booked time over the two minutes after the
    train heads for it;
  - done once the train has left the stop (or finished): those left walk
    away (task 06).
- **Coming**: one by one, each at a random place on its platform's walk area
  behind the edge zone, anywhere along the platform (a random seed of the
  platform side, 0.3 to 3.8 m further in, 0.3 to 1.9 m on a ground strip),
  facing the track more or less. A person appears only where the camera
  does not see it (off the screen or over 200 m away); all at once on the
  run's first frame, after a jump in time and when a station comes within
  reach.
- **Waiting**: standing (the idle clips), every 20 to 90 s a few steps
  (three in ten, within 3 m, never into the edge zone).
- **Random numbers** from the train, the stop and the platform item (and
  the person's number in its group), so runs repeat.
- **Walk areas for the simulation**: StationWalkAreas builds the stations
  within its reach of the camera whenever people are wanted, not only with
  View > Walk Areas (task 04's left item).
- **Moving**: Detour's crowd (`dtCrowd`, DetourCrowd from task 04's
  recastnavigation) on each station's walk area, at most 250 people a
  station; two filters: walking about (the edge zone costs 20 times) and to
  and from the doors (the edge zone like the rest).
- **Drawing**: one `Humans::Crowd` (task 03) for the route, its people
  steered (new `Crowd::Behaviour::Steered`: `steer` places a person and
  gives its speed, the walk played at that speed, idle below 0.3 m/s;
  `removePerson` gives its index to the next; `walkSpeedOf` the speed its
  walk carries it at). People are placed in the camera's tile each frame.
- **Who**: `Humans::HumanSet` (`src/tsre/humans/HumanSet.h`) reads
  `humans.dat` (below): the route's own, else TSRE's full set in the assets
  folder (`assets/humans`), else TSRE's basic set in appdata
  (`appdata/<version>/humans`; the user, 2026-11-12: appdata is in git and
  in the releases, assets is neither); each laid out as a route
  (`humans.dat`, `SHAPES`, `TEXTURES`). A `humans.dat` whose humans are all
  missing counts as none. The humans load through the route's shape
  library with their triangles kept for the impostors (task 03).
- **The setting** `core.content.people` (Content > Loading: "People at
  stations": Always, Route defined, Never; default Always), read once and
  when it changes (everybody made again).
- **Developer switch** `TSRE_PEOPLE_LOG=1`: the figures in the log as they
  change (stations, groups, people waiting, walking, boarding, getting off,
  boarded, got off, the frame's time), each station as it comes near (its
  place as a capture view takes it), each standing train's doors.

## humans.dat
In the route's main folder, as `carspawn.dat`; MSTS's header line optional;
the first list is the platforms'. A human is a block of its own (the user,
2026-11-12: it grows later, colours per region, a height range, kinds);
fields not known yet are skipped:

```
SIMISA@@@@@@@@@@JINX0h0t______

HumanList (
	Name ( "Passengers" )
	Animations ( "people_clips.glb" )
	Human ( Name ( "man.glb" ) Weight ( 1 ) )
	Human ( Name ( "woman.glb" ) Weight ( 2 ) )
	Human ( Name ( "young.glb" ) )
)
```

`Weight`: how often the human is picked against the list's others (1 when
not given; the woman above twice as often as the man). The files are looked
for in the folder's `SHAPES` as written (no case search), textures in its
`TEXTURES`. A human not there, or without a `Name`, is told in the log and
left out.

## TSRE's default humans: where they ship
The user, 2026-11-12: the full set in the assets folder (a download), a
basic set in appdata; both packs kept in humans-gen (`packs/full`,
`packs/basic`, made by its `scripts/make_packs.py`), as TSRE's assets folder
is not tracked.

- **Basic** (`appdata/0.7/humans`, in git): 1.91 MB, 1.44 MB zipped. The
  3k-triangle level the most detailed (the 15k level dropped, the other
  levels' coverages kept), no clips in the human files (the shared clips
  file has them), colour textures 1024² in 256 colours with their alpha,
  normal maps 512². At 10 m on a platform it looks as the full set does;
  about 0.45 MB a further human.
- **Full** (`assets/humans`, not in git): 17.5 MB, 15.4 MB zipped.
- The impostors are baked from the most detailed level with at most 4000
  triangles (the full set's level 1, the basic set's level 0).

What the three humans take now, and what a basic set could (zip at level
9, as a release packs them; "est." from the files' parts):

| Set | Zip |
|---|---|
| As now: level 0 15k triangles, textures 2048² (colour with its hair mask, normals) | 15.4 MB (textures 12.9, shapes and clips 2.5) |
| Textures 1024² | 5.3 MB |
| Colour 1024², normals 512² | 4.8 MB |
| Colour 1024², no normal maps | 4.6 MB |
| Colour 1024² in a 256-colour PNG (its alpha kept), normals 512², no level 0 (the 3k level the most detailed), no clips in the human files (the shared file has them) | about 1.5 MB (est.) |

The colour textures need their alpha (the hair's mask), so JPEG does not
fit. Each human file carries its own copy of the clips (0.4 MB) besides the
shared clips file. The impostors are baked from level 1 anyway.

## Checks
- Suite `station-people` (10 checks): humans.dat with and without the
  header, a missing human told, the setting's three modes; doors split from
  door parts (task 06).
- Suite `humans-crowd` (+4 checks, 31): steered people placed where put, the
  walker and the stander posed apart, a removed person not drawn and its
  index given again, the walk's speed.
- Routes, with `renderer-capture` (an activity or timetable set, the
  simulation run to a time, then stepped 0.25 s a frame) and the log:

| Route, run | Station | What happened |
|---|---|---|
| EUROPE1, `lclsrvce.act` 09:00:03 | Carlisle (the player's train at its first stop) | 25 waiting along the platform |
| PRUDNIK, `PSK_2026.timetable_or` 05:38:00 | Raszowa | two trains due: 20 and 12 waiting (counted per train) |
| PRUDNIK, the same, 08:34:20 | Prudnik | 10 waiting, on the side platform and the island |

Cost (Release, this machine): the whole of a frame's people (groups,
stands, Detour's crowd, the crowd's update and push) 0.4 to 1.3 ms with
about 35 people at one station; the crowd's part 0.1 to 0.4 ms. Larger
stations not yet measured; GPU times only on the user's hardware.

## Left
- The full set's download into `assets/humans` (as the template route's
  archive is downloaded).
- A platform with both sides set (people on both sides of one track): its
  people take the first side only.
- People of a station leaving the camera's reach are let go and made again
  when it comes back (at new places; not seen, 1.5 km away).
- Stops without a booked time get people only from when the train heads for
  them: a train standing there at the run's start has none.
- Later tasks: hidden sources (07), seats (08), preferences and counts (10).

## Review (the user, 2026-11-13)
- **People walked above the ground** ("20 cm or so", growing as they walk
  to the train). Recast's mesh stands up to a cell's height (0.1 m) above
  the floor it was built from (its voxels' tops; more by ledges, where a
  polygon's corner takes its highest cell's): at Raszowa 9.2 cm under
  everyone, the same under those walking to the doors and at the doors'
  outside points (logged with `TSRE_PEOPLE_LOG=1`). The last metres to a
  door are a script from the person to that point, so they walked at the
  mesh's height into the train too. Now `WalkArea::Built::floorAt` gives
  the floor's true height under a point (the level triangles the mesh was
  built from, in 2 m cells: the highest from 0.4 m below to 0.05 m above):
  people walking and waiting on the mesh, and each door's outside point,
  stand on it. walk-areas +2 (the mesh 5 cm above the test platform, the
  floor found to the millimetre, not the shelter's roof or the bench's
  seat). The log line: the mesh's height above the floor, on average, at
  most, where no floor was found, and for those walking to doors.
- **Turns were harsh.** A person's heading eases to the way wanted and
  turns at most 3 rad/s (`turned`): on the mesh it follows its agent's
  velocity smoothed over a quarter of a second (not every swerve), on a
  scripted way (to a door, in a car) it no longer snaps at the corners,
  and before the steps up into a car it turns to the door first. Agents
  accelerate at 2 m/s² (3 before).
- **More people in the platform's middle**: 60 % of those coming onto a
  platform about its middle (a bell, the mean of three random draws along
  it), the rest anywhere; later near its entrances (07).
- **Clips**: `walk_carry` (an invisible heavy box) and `walk_formal` (upright,
  arms swung back: "looks like on drugs") dropped in humans-gen; one walk
  left. Calmer idles and more walks need a source beyond Quaternius'
  Standard libraries (the user to choose).
- **More clips, by weight** (the user's choice, 2026-11-13: 100STYLE, CC BY
  4.0, "the more bizarre should show more rarely"): humans-gen cuts 10
  idles and 9 walks from 12 of its styles into loops (its `docs/clips.md`:
  the styles, the loops, the weights); 33 clips a human. Each clip carries
  the file's word, glTF animation extras `TSRE_use` (idle, walk, named:
  played by name only) and `TSRE_weight`; `Crowd` sorts clips by it (by
  name and speed without it, as before) and gives a person its idle and
  walk by their weights (`weightedPick`; weight 0 never); the impostor's
  idle poses go to the most often picked idles first. 100STYLE is credited
  in the About window (with its CC BY 4.0 link and the changes: cut into
  loops, retargeted; osm-data +1) and in the files' `asset.copyright`. humans-crowd +9 (each human's 10 walks
  going the way its `walk` goes; the file's word read, the shared file's
  copies keeping it, 4000 people sharing the idles as their weights, none
  standing in a named clip; the counts: 20 walks, 26 idles with the man's
  own and the shared file's); gltf-skin's shared-clips
  check covers a 100STYLE walk and idle too.
  These idles barely move (real standing: over 10 s the body sways 2 to
  3 cm, the head turns 4 to 8 degrees). Livelier waiting, later: own idles
  made in Blender (weight shifts, looking round, a watch); switching a
  waiting person's idle every 10 to 30 s with a blend; heads turning to a
  train or someone passing. Their walks are slower than UAL's (the man
  0.62 to 1.19 m/s, `walk_rushed` 2.06), the capture room being small; a
  walker keeps its clip's own speed.
- Getting on and off the EN57 looks right: left as it is.
