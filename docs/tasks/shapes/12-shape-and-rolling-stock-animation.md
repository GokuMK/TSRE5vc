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

Plus: Shape Viewer controls (play, begin/end, doors open/closed).
Next task, not this one: doors opening at the correct platform side (the
side from the world file's platform object, as Open Rails takes it).

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
10 s before leaving; timetable trains never open automatically.

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

## Steps

- [x] `ShapeAnimation` evaluator; both backends on it; quaternion convention.
- [x] Part and pose API on both MSTS backends; states released and reused.
- [x] Tests (`shape-animation`): evaluator rules; on the 129 stock trainset
      shapes, 254 of 255 keyed parts at rest at frame 0, both backends posing
      all 1,020 sampled poses alike, looping states as every part at the
      same frame.
- [ ] `CarAnimation`: part roles by name; wheels, bogies, running gear,
      two-state parts.
- [ ] Placement on pivots; bogies turned; wheels turned by the simulation's
      distance; pantographs up on simulated electric locomotives.
- [ ] Shape Viewer: Animation menu (play, start/end pose, pantographs,
      doors, mirrors, wipers).
- [ ] Captures: stock steam (Scotsman, Pendennis), OR_PSK electric units
      with doors and pantographs, a train on a curve.
