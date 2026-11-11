# 05 - MaSzyna sounds: vehicles, cabs, controls

## Objective

A MaSzyna vehicle sounds as its `.mmd` says: the vehicle's sounds
(`sounds:` and DynObj's `internaldata:` keys), the cab's when it is ridden
(Train's `internaldata:` keys) and the clicks of its cab controls, read from
the `.mmd` and its includes, its files found as MaSzyna finds them and
played the way MaSzyna plays them (begin, loop and end; tables crossfaded by
a value; random sets; range; placement and soundproofing; offsets), driven
by the loco script's values and events and, for what every car does, by the
host.

Status 2026-11-11: steps 1 to 7 built on `feature/maszyna` (the user's go
2026-11-10), not committed; [results](#results-2026-11-11) and [what is
left](#open-work-left).

Review behind it: [MaSzyna sounds in `.mmd`](reports/mmd-sounds.md), part A
the semantics from MaSzyna's code, part B the pack's numbers.

Not in this task:
- **MaSzyna's wheel clatter** (`wheel_clatter:`, by rail lengths): read,
  not played; clatter at rail joints may come from the world later, for
  every car alike (the user, 2026-11-10).
- **The open window** column of soundproofing: no window view in TSRE yet
  (Open Rails has one); the column is kept for it.
- **Passenger announcements** (`announcements:`, `departuresignal:`): need
  door speakers and the timetable's station names.
- **Radio messages**, captions, MaSzyna's volume categories.
- **Uniform ranges** for every sound kind in TSRE: later fine tuning (the
  user); MaSzyna's own distance model for now (design 6).

## Decisions so far

The user, 2026-11-10:
- **Its own class.** The `.mmd` makes its own sound handling class, not a
  `.sms` model (nor sounds handled inside `Eng` or the `.mmd` reader).
- **Scripts send sounds as they do now.** Lua loco scripts already publish
  values and send `sound` messages; MaSzyna's are more of them and more
  complex, through the same path.
- **Running noise as ENG cars have it:** the route's running noise per
  track type (`TrackSound`, the route's `ttype.dat` sounds by sound
  regions) plays for MaSzyna cars too, and their own running noise
  (`outernoise:`, the cab's `runningnoise:`) plays on top of it, as an
  ENG's own rolling streams do (588 of OR_PSK's 1,080 `.sms` have one,
  e.g. `189eng.sms`'s `x_Rollen.wav`; original MSTS stock has none).
  `wheel_clatter:` is skipped. Everything else plays on top as well:
  `curve:`, `brake:`, `brakesound:`, the cab's `huntingnoise:`,
  `resonancenoise:`, `windsound:`, `rainsound:`. **A developer switch**
  for now, not a user setting, since the final choice needs audiovisual
  tests first (design 4).
- **Ranges:** to be uniform later; for now as the design says.
- **Open window:** to be done, may be left for later.

The user, 2026-11-10, accepting the proposals:
- **Ogg Vorbis** through a vendored `stb_vorbis` and an `OggFile` reader
  beside `WavFile` (design 1).
- **Who drives which sound:** the host for what every car does, wagons
  included; `maszyna.sounds` for the loco's own (design 4).
- **Read as MaSzyna reads**, quirks included, typos only warned (design 2).
- **Random sets** picked per car and key, the same at every load
  (design 2).
- **Named sound values** passed on with the car's sound input, and
  `send("sound", name)` for one-shots (design 4).
- **`.ogg` decoded on a worker** in `SoundBank`, a sound silent until its
  clip is ready (design 7).

## What there is

- **TSRE's sounds** (`src/tsre/sound/`, sound tasks 01 and 02): `SoundSource`
  plays an MSTS `.sms` (`Sms::File`: groups, streams, triggers, curves on a
  fixed set of car values, `SoundValues`). Below it, reusable as they are:
  `SoundBank` (clips read once per file, a mono copy for 3D), `SoundClip`
  (parts: intro, sustain, release), `StreamPlayer` (a queue of clips in
  one-shot, loop or loop-release modes, its speed), `SoundVoice` (one player
  as a miniaudio sound, placed or not, its gain and position), and
  `SoundSystem` (the listener, its velocity, the clock, the registered
  sources, `TSRE_SOUND_LOG`). `WavFile` reads PCM and float waves only;
  miniaudio is built without decoders.
- **A car's sounds** (`Eng::updateSounds`): the ENG's outside `.sms`, the
  cab's on the lead car, and `TrackSound` (the route's `ttype.dat` sounds by
  TDB sound regions, an outside and an inside `.sms` per type; joint and
  switch triggers read but not played). Values from the car (speed,
  distance, wheel rpm, curve force) and, on the driven train and heard AI
  shadows, from the script (`Sim::carSoundInput`: the brake cylinder,
  Variable1-3, engines, tractive effort). Events: `Eng::soundEvent(n)`, Open
  Rails' numbers, from scripts' `send("sound", n)` and the host (doors 105
  and 106).
- **The listener** (`RouteEditorGLWidget::updateSound`): the camera, its
  view (outside, cab, passenger) and the car it rides or follows.
- **MaSzyna's text** (`MaszynaText`): tokens through `include ... end` with
  `(pN)`; a set `[ a b c ]` gives its first. `MaszynaCab` reads cab controls
  and skips their sound fields. `MaszynaPaths::places`: a name in the
  vehicle's folder, then the shared folder (textures today).
- **Scripts** publish `out.values` each step (`o.variable1 = ...`), read by
  the host by name; the cab's values come from `maszyna.cab`.

## Design

### 1. Ogg Vorbis

- `thirdparty/stb_vorbis/`: `stb_vorbis.c` v1.22 (public domain or MIT, as
  miniaudio), its licence and a README (version, source, checksum), a row
  in `thirdparty/README.md`. Compiled in one TSRE file with warnings off,
  as `Miniaudio.cpp`, with `STB_VORBIS_NO_STDIO` and
  `STB_VORBIS_NO_PUSHDATA_API`: TSRE reads the bytes, it decodes them from
  memory. No new link dependency, so the MinGW build stays as it is.
- `OggFile::parse` / `load` beside `WavFile`, giving the same `WavData`
  (16-bit interleaved, rate, channels, no loop points).
- `SoundBank::get` picks the reader by the file's first bytes (`OggS`,
  `RIFF`), not its name: any TSRE sound may be an `.ogg`.
- A mono copy averaging the channels for MaSzyna (MaSzyna mixes down;
  `WavFile::toMono` keeps the second channel as Open Rails does). Only 18
  of the 4,190 files used are stereo.
- No FLAC: the pack has none (B.5). `dr_flac` the same way if one comes.

### 2. Reading (`MaszynaSound`, C++)

- **Where:** `src/tsre/maszyna/MaszynaSound.h/.cpp`, read once per `.mmd`
  through `MaszynaText` (includes, `(pN)`), shared by every car of the
  type, made lazily like `MaszynaVehicle::cab()`.
- **What:** a `Definition` per key: samples (begin, main, end, or the
  table's thresholds with sample and pitch), set alternatives, range,
  placement, offset, its own soundproofing, amplitude and frequency factor
  and offset, crossfade, pitch variation. The vehicle's `sounds:` keys,
  DynObj's and Train's `internaldata:` keys, the 30-number soundproofing
  table, and per cab control `soundinc`/`sounddec`/`sound<N>` (with the
  lamps' `soundinc`/`sounddec`), kept with the control in `MaszynaCab`.
- **Keys from a table**, as the cab labels: each key's legacy form
  (single or begin main end) and numbers (range, amplitude, frequency), its
  default placement and range, its load-time changes (the divisions by
  `Vmax`, `nmax`, `RVentnmax`, the presets of `transmission:` and
  `unbrake:`), from part A section 4 and 5.
- **As MaSzyna reads**, quirks included, since authors tuned their sounds
  in MaSzyna: the fixed number counts (a short entry eats the next key),
  `pitch<N>` only after its `sound<N>`, the last of a doubled key, unknown
  keys passed over. The known typos (`gengwaydetach:`, `soudmain:`,
  `placement: interal`) get one warning each in the log, not a fix.
- **Random sets** (`[ a b ]`, `soundset:`, `include [ ... ]`): MaSzyna picks
  at random at every load. Proposed: picked per car, seeded by the car's
  UiD and the key, so cars of a type differ and a car sounds the same at
  every load. `MaszynaText` gets the whole set where a sound asks (today
  it gives the first), the vehicle's models keep the first.
- **Files** (`MaszynaPaths`, a sound flavour): lower case (the format's
  rule), the written extension dropped, then `.ogg`, `.flac`, `.wav` in
  the vehicle's folder (vehicle sounds only), as a path when it has `/`,
  then in `SOUND/MASZYNA` (MaSzyna's `sounds/`). No search beyond that.

### 3. Playing (`MaszynaSounds`, C++)

- **One per car**, made by the definition (`VehicleDefinition::soundsMaker`
  like `cab3DMaker`), held by `Eng` beside its `.sms` sources and fed by
  it: place and frame, values, events, the listener. `Eng` knows only a
  small interface (`CarSounds`: `update`, `event`, `release`), so it holds
  no MaSzyna code.
- **Registered with `SoundSystem`** like a `SoundSource`, both behind one
  small interface (`update(listener, clock)`, `release()`), the
  `TSRE_SOUND_LOG` summary counting both.
- **Emitters** per definition and place: per traction motor, per
  pantograph, per coupler end, per door side, as MaSzyna copies them
  (part A 4.6 for the default places; `locations:` gives motors, bogies and
  doors). Offsets are in the car's frame (MaSzyna's +x left, +y up, +z
  front, TSRE's drawing space as for `.e3d`), turned with the car.
- **Basic sounds:** a `SoundVoice` with begin and main queued, the main
  looping when asked; stop cuts the main and plays the end on a second
  voice (MaSzyna does not wait for the loop's end).
- **Tables:** the playing entry or the two crossfading ones, each its own
  voice, gain by MaSzyna's curve (A.2.3), speed lerped toward the
  neighbour's `pitch<N>`; a one-shot table plays the entry the value falls
  in (relays, brake cylinder).
- **One-shots** may overlap (no flags), exclusive ones restart only when
  done; `play`, `stop`, `gain`, `pitch` as MaSzyna's `sound_source`.
- **Gain** = the driver's gain (clamped 0 to 2) × crossfade × soundproofing
  × distance; **speed** = pitch × pitch variation (0.1 to 10) × Doppler.
- **Pause**: everything stops while the simulation is paused.

### 4. What drives each sound

MaSzyna drives each key in code (part A 4 and 5); TSRE splits that code:
- **The host, for every car** (C++ in `MaszynaSounds`, from what `Eng`
  has, wagons included, which run no script): `outernoise:` (speed, the
  brake), `curve:` (speed, curve radius, sideways acceleration), `brake:`
  squeal and `brakesound:` (speed, the car's brake force from its
  cylinder), `unbrake:` (the cylinder falling), `releaser:`, couplers and
  hoses (the host's coupling and uncoupling), `bufferclamp:` and
  `couplerstretch:` (the coupler's slack, when the host has it), doors
  (the host's door events), `derail:`, `startjolt:`, and in the ridden
  cab `runningnoise:`, `huntingnoise:`, `resonancenoise:`, `windsound:`,
  `rainsound:` (when TSRE's weather rains).
- **The loco script** (Lua, a module `maszyna.sounds` beside
  `maszyna.cab`, its entries replaceable per loco): `tractionmotor:`,
  `engine:`, `ventilator:`, `motorblower:`, `compressor:`, `converter:`,
  `small-compressor:`, `inverter:`, `heater:`, `battery:`, the relays
  (`relay:`, `shuntfield:`, `wejscie_*:`), `linebreakerclose:`/`open:`,
  `ignition:`/`shutdown:`, pantographs, `sand:`, `horn*:`,
  `emergencybrake:`, `brakeacc:`, `slipperysound:`, and the cab's
  `buzzer:`, `airsound*:`, `localbrakesound*:`, `tachoclock:`,
  `radiostop:`, `slipalarm:`. Its formulas follow part A's, on the
  script's own state (`s.dc`, the brake valve, the compressor...).
- **How it reaches the class:** continuous sounds as values in
  `out.values` by name (`sound.tractionmotor` gain and pitch or table
  value, nil when off), which the host passes on with the car's sound input
  (`CarSoundInput` gets a list of named values); one-shots and edges as
  `send("sound", "relay")` with a name, beside Open Rails' numbers. Heard
  AI trains run the same scripts (sound task 02), so they sound the same.
- **Running noise, a developer switch** (the user: the final choice after
  audiovisual tests; developer options stay out of the user's settings):
  the environment variable `TSRE_MASZYNA_RUNNING_NOISE`, read once, as
  TSRE's other `TSRE_*` switches:
  - `both` (default, also when unset): the route's track-type sounds
    (`TrackSound`) and the car's own `outernoise:` and cab
    `runningnoise:` on top, as ENG cars with their own rolling streams;
  - `route`: the route's only, the car's own not played;
  - `vehicle`: the car's own only, no `TrackSound` for MaSzyna cars.

  `wheel_clatter:` is not played in any of them. Once decided, the switch
  goes and the choice stays.

### 5. The cab

- **Only on the ridden car** (as `Eng`'s cab `.sms`), from the cab ridden
  (`cab1` or `cab2`); made again at a cab change.
- **Control clicks** follow the control's value, as MaSzyna does, so a
  key, the mouse and the script click alike: `MaszynaCabPose` sees each
  control's value each frame; on a change (to 0.01) it plays `sound<N>` for
  a whole value N, else `soundinc`/`sounddec`, else the fallback (`ctrl:`
  for `mainctrl`, `ctrlscnd:`, `reverserkey:`, `switch:`,
  `pneumaticswitch:`). At the control's part, range 7.5 m, internal.
- **Lamps** play their `soundinc`/`sounddec` when they light or go out.
- **Cab sound places** as MaSzyna's defaults (part A 5.3): on the
  controllers, the brake handle, the alerter lamp, else the cab's middle.

### 6. Distance, placement, soundproofing

- **Distance**, MaSzyna's model for now: full within range/16, then
  inverse distance with rolloff 1.75, faded to nothing between range and
  1.25 × range; not started beyond min(2,750, 5 × range), stopped beyond
  min(3,000, 7.5 × range); range -1 at the listener. A parameter of the
  emitter, so a uniform TSRE model can replace it later.
- **Soundproofing** by MaSzyna's 5 × 6 table (the vehicle's, or the
  sound's own row), the listener's place mapped from TSRE's view: in this
  car's cab = its front or rear cab column by the cab ridden; a passenger
  view in this car = the middle column ("cab 0"); the camera following
  this car from outside = column 4; free, or in another car = column 5,
  then × the listener car's external row at its own column. The window
  column (3) unused until there is a window view.

### 7. Speed and memory

- Clips are read once per file and shared (`SoundBank`) by every car of a
  type; a car's sounds are read when it is first heard, as `.sms` today.
- ET22's `201e.mmd` names 292 files (every alternative of its sets
  counted): 831 s of sound, mean 2.85 s, the longest 174 s (rain).
  ffmpeg decodes the rain file in 0.18 s, about 1,000 times real time;
  `stb_vorbis` is slower, estimated 250 to 500 times: one to three
  seconds for a whole vehicle on one core, too long for the game thread.
  So `.ogg` clips are decoded on a `QThreadPool` worker (not `std::thread`,
  for MinGW), a sound silent until its clip is ready; `.wav` stays read
  at once as now. Measured in Release at step 1.
- Decoded 16-bit mono PCM: ET22's 831 s at 44.1 kHz would be about 73 MB
  with every alternative, less with one per set; shared by its cars.

## Steps

1. Ogg Vorbis (design 1, 7): vendored `stb_vorbis`, `OggFile`, `SoundBank`
   by content, `.ogg` decoded on a worker; tests on a small `.ogg` and on
   pack files; the decoding time in Release.
2. The reader (design 2) and the sound lookup; suites `mmd-sound`
   (generated `.mmd`: both syntaxes, every inner key, the quirks) and
   `mmd-sound-corpus` (the pack against part B's numbers: definitions,
   99.98 % found, the 11 missing names, no error).
3. The player (design 3, 6): `MaszynaSounds` with basic, table, set,
   one-shot and exclusive sounds, distance and soundproofing; tests
   rendering without a device (as the sound suites do).
4. The host's sounds for every car (design 4): running noise, curve,
   brakes, couplers, doors, the cab's noises; `TSRE_MASZYNA_RUNNING_NOISE`.
5. `maszyna.sounds` and the named values and events (design 4); ET22's
   electric keys first, then diesel and induction.
6. The cab (design 5): cab sounds and control clicks.
7. ET22 heard: a drive with `TSRE_SOUND_LOG`, the heard AI case, and the
   user's listening on their hardware.

## Verification

- Suites above plus the existing sound suites unchanged; `all`, ctest and
  the app suites.
- `TSRE_SOUND_LOG` on an ET22 drive (USA2 test root, `nnmxdgds.act`):
  sources, voices and clips counted; the keys started and stopped in order
  (pantograph up, line breaker, relays through the notches, motor and
  ventilator with speed, compressor cycling, brakes).
- A heard AI MaSzyna train within 1,500 m: its shadow script's sounds.
- The time to read ET22's sounds, in Release.

## Results (2026-11-11)

Built on `feature/maszyna`, checked on the worktree's build (no build type)
and timed in Release.

- **Ogg Vorbis** (step 1): `thirdparty/stb_vorbis/` (1.22, stb commit
  1ee679ca, its README and licence), `StbVorbisConfig.h` and
  `StbVorbis.cpp` (warnings off), `OggFile` beside `WavFile`. `SoundBank`
  picks the reader by the first bytes; `SoundBank::Channels` (Stored,
  Second as Open Rails, Mixed as MaSzyna) and `WavFile::mixToMono`; Ogg is
  decoded on the bank's own `QThreadPool` (one or two threads), the clip
  null until it is there (`get(..., &loading)`, `waitForLoading`), cleared
  banks dropping late results. Release: 580 pack files (2,069 s of sound)
  in 1.25 s on one core, about 1,650 times real time; 50 files through the
  bank's workers in 45 ms. ET22's 831 s would be half a second on one core.
- **Reading** (step 2): `MaszynaSound` (`read`, `readValue`, `find`,
  `Physics::of`), MaSzyna's key tables (DynObj's `sounds:` and
  `internaldata:`, Train's), both forms, `locations:`, the 30-number
  table, `huntingspring:`. `MaszynaText` got `chooser` (random sets and
  includes, per car) and `keepBrackets`; `MaszynaCab` keeps a sound
  value's `[ set ]` whole. Pack (`mmd-sound-corpus`): 1,461 `.mmd`, 1,440
  with vehicle sounds (the review's count), 243,372 samples 99.983 % found
  (cab controls' 117,005 of 117,011), the names missing a subset of the
  review's 11, `gengwaydetach:` told in 801 vehicles (the review's count,
  the 37 whose `}` swallows `internaldata:` included).
- **Playing** (step 3): `SoundEmitter` (what `SoundSystem` updates;
  `SoundSource` one of them), `CarSounds` (what `Eng` holds and feeds:
  `set`, `event`, `control`, `trackSounds`), `MaszynaEmitter` (MaSzyna's
  `sound_source`: begin and main in one voice, the end on stop, one-shots
  overlapping unless exclusive, tables crossfaded and pitched, MaSzyna's
  distance model and cutoffs, OpenAL's gain cap), `MaszynaSounds` (places:
  motors, bogies, doors by side, couplers, pantographs, the powertrain at
  half height, the cab's middle; soundproofing by the listener's column,
  through the listener car's outside row; Doppler at MaSzyna's 0.25).
  `VehicleDefinition::soundsMaker` makes it per car, seeded by the car's
  UiD (`Eng::setSoundOwner` from its consist).
- **The host** (step 4): running noise per bogie (kept from those aboard
  in the cab or passenger view), curve squeal from the sideways
  acceleration, shoes and squeal from the cylinder's share of MaxBP, the
  release hiss from its falling rate, the start jolt, doors by side
  (`SimulationController` sends `dooropen:left` and the like besides Open
  Rails' numbers), in the ridden cab its running, hunting, resonance and
  wind noises and brake noise. `TSRE_MASZYNA_RUNNING_NOISE` as decided.
- **The script** (step 5): `maszyna.sounds` (traction motors with
  MaSzyna's momentum and smoothing, resistor ventilators by RVent type,
  relays by position, shunt field, the resistor-free notch and the
  series-parallel step, line breaker, pantographs, compressor, converter,
  horn, sander, releaser; in the cab the brake valve's exhaust and filling,
  the local brake's hisses, the speed recorder), on
  `electric-resistor-mmd.lua`. `vehicle.sounds` and `vehicle.cabSounds`
  give it the factors (`MaszynaVehicle::sounds()`, each set's first);
  `sound.<key>` values hold sounds (false releases them: the host keeps
  values), `send("sound", "key[:pitch[:gain]]")` and `key:side` play
  one-shots. `maszyna.dcmotors` keeps the motors' torque (`d.mm`).
- **Cab controls** (step 6): `CabPose::controlPlace` (MaSzyna's cab: the
  part's offset from the model's root), `CarSounds::control` from
  `Eng::poseCab` on each changed value; `MaszynaSounds::control` plays as
  `TGauge::UpdateValue` (first value silent, a hundredth's change, a whole
  value's `sound<N>`, else `soundinc`/`sounddec`, else the cab's `ctrl:`,
  `ctrlscnd:`, `reverserkey:`, `pneumaticswitch:` or `switch:`, exclusive
  once a value between whole ones came) and lamps as `TButton`; the cab's
  `ctrl:` and the like moved to their controls.
- **Outside the MaSzyna code:** TSRE's listener was always outside
  (`playSoundView` never set since sound task 01). In a 3D cab it now hears
  from the cab (`listener.view` Cab, `listener.cab` front or rear,
  `listener.train`), so an ENG cab's `.sms` streams for the cab camera
  play there too.
- **Checks** (step 7): suites `sound` (111, Ogg cases added),
  `ogg-corpus`, `mmd-sound` (34), `mmd-sound-corpus`, `mmd-sound-player`
  (38); `TSRE_SOUND_LOG` names a heard MaSzyna car's keys and what started.
  Release: the pack's 1,461 `.mmd` (with their `.fiz` and cabs) read in
  2.4 s, ET22's sounds for a car in 1.7 ms. All app suites: 98 pass, the 7
  needing arguments as before; ctest 11 of 11.
  ET22 driven (USA2 test root, `nnmxdgds.act`): in the cab the motors,
  ventilator, converter, relays, horn, sander, the cab's running noise and
  speed recorder, no outside running noise; at the front, the running
  noise of ET22 and its wagons and no cab noise; braking, the shoes on
  every car. The mouse capture: the line breaker buttons, the main
  controller's and the brake handle's positions, switches clicking.

## Open: work left

- **Not driven yet** (no TSRE model of them): the alerter's buzzers,
  radio stop, slip alarm, distance counter, the emergency valve, the
  brake accelerator, sliding wheels, the pantograph compressor, spring
  brakes, loading, wipers, derailing, wheel flats.
- **Diesel and induction locos:** `maszyna.sounds` has the series-motor
  keys; `diesel-mmd.lua` and `electric-induction-mmd.lua` do not call it
  yet (engine, revving, turbo, pumps, fans, transmission, ignition and
  shutdown, inverter, AC motors, blowers).
- **Couplers, hoses, buffers:** TSRE's coupling events are the train's
  (`TrainEvent::Coupled`), not a coupler's end; no slack for buffers and
  stretching.
- **Rain** in the cab: needs the weather's precipitation at the car.
- **Doors** only on the driven train (as the Open Rails events).
- **Pantographs' places** from the `.mmd`'s count at ±length/4 (MaSzyna
  takes them from the model).
- **Scripts' factors** are each set's first alternative; a car's own picks
  can differ (only where random includes change factors).
- **Track quality, tunnels, bridges** do not scale the running noise (no
  such data on TSRE's tracks).
- **AI trains** run the same scripts as shadows; their MaSzyna sounds are
  untested.
- **Timing:** a car's sounds are read on the game thread when first heard:
  ET22's in 1.7 ms in Release (files in the system's cache), so a 40-car
  MaSzyna train about 70 ms at once; on a worker if that shows.

## Questions

None open: all answered by the user on 2026-11-10 (decisions above). The
build waits for a go.
