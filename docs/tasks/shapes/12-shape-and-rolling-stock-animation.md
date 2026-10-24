# Task 12 - Shape and rolling stock animation

Status: in progress (branch `feature/animations`, from 2026-10-23).

The user's request (2026-10-23): TSRE's shape animations are disabled or
broken; make animated trains come alive in the simulation's rendering.
Two kinds of animation:

1. Driven by the engine: bogies turning with the track, wheels turning with
   speed. The car itself placed on its bogie pivots, not its ends.
2. Keyframes in the shape: doors, pantographs, mirrors, wipers, steam
   running gear. TSRE's keyframe posing is limited and broken, and differs
   from Open Rails' runtime approach; choose a future-proof solution.

Plus: Shape Viewer controls (play, begin/end, doors open/closed), and
(2026-10-24) doors opening at the platform side, the side asked of the
world file's platform object when they open, nothing when its tile is not
loaded.

## Before

- `SFileLegacy` and `SFileComplex` each posed keys their own way, both with
  one looping frame per shape state (world objects with the Animate flag):
  no part could be posed on its own. Rolling stock drew shape state 0,
  shared by every car of a shape, never animated.
- Both multiplied a key's rotation onto the matrix's own rotation; Open
  Rails replaces it. Parts whose keys repeat their rest rotation turned
  twice as far (the 183's cab gauges: 84 degrees became 168).
- Both read key quaternions as (-x, y, -z, w); the rest matrix of every keyed
  stock part matches its frame 0 key only as the conjugate (-x, -y, -z, w):
  158 of 158 parts in 46 stock shapes plus the 183's 3D cab, against 109 for
  the old reading (it fails on turns about y: bogies).
- Legacy left out `slerp_rot` keys and, after a part's last key, went back
  to its first.
- Cars were placed on the chord between their ends.

## Open Rails (reference)

Upstream master 8aafb3147, `Source/`. Viewer3D/Shapes.cs (SH),
RollingStock/MSTSWagonViewer.cs (WV), AnimatedPart.cs (AP),
Orts.Simulation/.../TrainCar.cs (TC), Train.cs (TR).

Keyframes (SH:336-415, ShapeFile.cs:1403-1574):
- Only `animations[0]`; anim_node i belongs to matrix i (names unused).
  Controllers `linear_pos` and `tcb_rot` (its keys `slerp_rot` or
  `tcb_key`, TCB parameters ignored: plain slerp).
- A part at frame f: from its rest matrix; the last key at or before f and
  the next one, `clamp((f - f1) / (f2 - f1), 0, 1)`; before the first key the
  first, after the last the last. Rotation replaces the 3x3 (translation
  kept), position replaces the translation. A part without keys keeps its
  rest matrix. Animating a part poses its children at the same frame.
- World objects with the Animate flag loop all parts at the header's frame
  rate, wrapping at its frame count.

Rolling stock:
- Placement (TC:2987-3129, 3831-3901): the track sampled under each axle;
  a least-squares line of position against shape z per bogie (its axles)
  and for the body (its own axles plus each bogie's pivot on that bogie's
  line). Two bogies: the body on the chord between their pivots. No
  wheels or bogies on a wagon: two virtual axles at its ends (the chord
  between the ends). Roll: the average at the samples. The car spans shape
  z from CoG.z - L/2 to CoG.z + L/2.
- Bogies (WV:511-533, 888-925): `BOGIE` or `BOGIEn` (length <= 6); turned
  to the track at their pivot, relative to the body; their own rotation and
  keys replaced, their translation kept.
- Wheels (WV:484-510, 809-877): `WHEELS` + 1 to 3 characters, under 3 m
  high (taller ones are fans: running gear); the 7th character the bogie.
  Turned about their local x by distance / radius (wagons: Wagon
  WheelRadius; locomotives: Engine WheelRadius, a steam locomotive's
  non-driven wheels Wagon WheelRadius), keys ignored. A 7-character
  `WHEELSn` with keys is running gear instead.
- Running gear (WV:663-671, 825-830; AP:184-192): every keyed part no rule
  claims (not matrix 0), and keyed `WHEELSn`: one wheel revolution of the
  driving wheels is the group's last key frame (locomotives); on wagons
  `FrameRate * 8 / 30` frames a revolution.
- Two-state parts move at 1 key frame a second between frame 0 (closed,
  down) and their last key frame (open, up): doors `DOOR_A/B/C` right,
  `DOOR_D/E/F` left (car-local; a flipped car's sides swap in the
  simulation), pantographs `PANTOGRAPH{BOTTOM,MIDDLE,TOP}{1,2}[A,B]` and other
  `PANTO*` by digit, mirrors `MIRROR*`, cab side windows. For an unflipped
  car shape pantograph 1 follows simulator pantograph 2. Wipers loop at
  `FrameRate * 1.5 / 30` frames a second while on.
- The last key frame of a group: over its parts and their descendants.

Doors (TR:16664-16729, AITrain.cs:1853-1926, Signals.cs:4238-4242): the side
from the world file `Platform` object's `PlatformData` (0x2 left, 0x4
right, relative to the platform's TDB item order, swapped when the stop
references the `ffff0000` end); AI trains open 4 s after arriving and close
10 s before leaving; timetable trains never open automatically (TSRE's do,
below).

## Design

- `ShapeAnimation` (`src/tsre/shape/ShapeAnimation.h`): one evaluator for
  both MSTS backends, Open Rails' rules above; keys converted once at load
  (`fromMsts`, the conjugate). Channels are per part and backend-neutral, so
  glTF node animations (translation, rotation, scale by time) can map onto
  them later.
- `ComplexShape` parts and poses: `partCount/Name/Parent/Rest/LastFrame`,
  `animationFrames/Rate`; per state `setPartFrame` (a part at a frame of its
  keys), `setPartTransform` (a transform of its own: wheels, bogies),
  `clearPose`, `releaseState` (states are reused; a released state keeps its
  matrices and packets, which the renderer may still hold that frame).
  Looping world objects (`setAnimated`) stay as they were, now on the same
  evaluator.
- Rolling stock (`CarAnimation`): each car on the track its own shape state;
  its parts classified once by Open Rails' name rules; driven by the car's
  distance travelled (wheels, running gear) and its controls (pantographs,
  doors, mirrors, wipers), two-state parts moving at 1 key frame a second.
- Placement: the track between the car's two ends (as the simulation and the
  editor place them) as a cubic curve from their positions and tangents;
  sampled at the bogie pivots and body axles; the body fitted as Open Rails
  fits it; each bogie turned to the curve's tangent at its pivot. No walking
  of the track across junctions (a placed traveller does not know which way
  its train came).

### Later: placement from the simulation

The user's choice (2026-10-24): keep the cubic for now; move later, when the
physics needs the vehicles' pivots anyway. The simulation core knows each
train's way through every switch, so it can place each vehicle's two
travellers on its bogie pivots (as Open Rails samples the track under each
axle with its train's traveller) given each vehicle's pivot distances (from
its shape, once loaded). The body is then the chord between them, as the
drawing already takes its two places, and each bogie turns as the track
position on the TDB node says (its angle and slope): no cubic, no fit.
Editor consists walk to their pivots with the activity's junction map, as
they already do to their rear end.

## Steps

- [x] `ShapeAnimation` evaluator; both backends on it; quaternion convention.
- [x] Part and pose API on both MSTS backends; states released and reused.
- [x] Tests (`shape-animation`): evaluator rules; on the 129 stock trainset
      shapes, 254 of 255 keyed parts at rest at frame 0, both backends posing
      all 1,020 sampled poses alike, looping states as every part at the
      same frame.
- [x] `CarAnimation` (`src/tsre/trains/CarAnimation.h`): part roles by Open
      Rails' names; wheels turned about x (top toward the shape's front going
      forward), running gear a driving wheel revolution to its last key frame
      (wagons: the frame rate * 8 / 30), two-state groups a key frame a second
      (at once the first time), wipers looping; only parts away from their
      rest pose are posed, so standing cars on straight track keep sharing
      their packets. `TSRE_ANIMATION_LOG` prints what each car's animation
      found.
- [x] Placement (`src/tsre/trains/CarPlacement.h`): the track between the
      car's ends as a cubic from their places and ways (the way from a short
      step of the end's traveller), sampled at the bogie pivots and body
      axles, the body fitted to them; each bogie turned to the cubic's way at
      its pivot. On a 150 m curve: pivots within 1.2 mm of the track, bogies
      along it (`car-animation`).
- [x] Simulation: wheels and running gear by the distance travelled where the
      train is drawn (`TrainFrame::drawnTravelled`); an electric locomotive's
      pantograph 1 up as Open Rails raises an AI train's (for an unflipped
      car with both, the shape's 2).
- [x] Shape Viewer: Animation menu (Play at 15 km/h, a shape at its last
      frame, pantographs 1 and 2, left and right doors, mirrors, windows,
      wipers); captures take "animation", "animationEnd" and "travelled", and
      renderer captures "trainVehicle", "trainEye", "trainAt" (close views).
- [x] Captures: the 183 (pantograph 2 up in a simulated train; both up in
      the viewer), the PSK EN76 (plug doors open), the Scotsman's rods a
      quarter turn on; the simulation-trains capture's CPU 4.5 -> 4.8 ms.
- [x] Doors at station stops (activity trains; Open Rails' timetable trains
      open none): a stop keeps its platform's track item (PlatformStartID);
      when the doors are due (4 s after arriving until 10 s before leaving,
      as Open Rails' AI trains) the platform's world object is looked for in
      the loaded tiles (its PlatformData: 0x2 left, 0x4 right, from the item
      marked ffff0000 the other way round), found sides kept by track item;
      its tile not loaded: no doors, looked for again each second (on every
      draw, the simulation paused or not) until no longer due. A car's sides
      swapped where it faces against the way the train runs. Checked: the
      PSK EN76 at Scotby (EUROPE1 lclsrvce, 09:08) opens its left doors to
      the platform.
- [x] Doors at timetable stops too (user, 2026-10-24: better than Open
      Rails): a stop keeps the platform item where its path leaves the
      platform, the end an activity's PlatformStartID names (all 191 stops
      of EUROPE1's lclsrvce); with the end met first they opened on the
      track's side. Stops behind the path's start, beyond its end or inside
      a reversal keep the item those lookups find (their side not checked
      on content). Checked: Prudnik's PSK_2026 timetable, 0749 Bielsko-Biala
      - Sosnowiec (KSL EN57-730) at Katowice (07:56-07:58) opens its left
      doors to the platform on all three cars, the right ones shut (captures
      with the views' new `simulateTo`, the simulation run on a few seconds
      between views); timetable-trains checks the stops' items.
- [ ] The user's review on hardware.
