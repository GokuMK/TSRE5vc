# Task Sound 01 - Sound System

Status: steps 1-4 and the motion half of step 5 done (2026-10-12), branch
`feature/sound`, not merged. The rest of step 5 needs a train simulation;
step 6 (environment) next.

## Objective

TSRE plays MSTS sound content the way MSTS and Open Rails do: engine and
wagon sounds from their `.sms` files driven by the simulation's values,
world sound sources and sound regions placed in the route, and environment
sounds. It starts in the Route Editor's play mode and is the base for the
game later.

## What exists

`src/tsre/sound` (about 1300 lines, OpenAL, written 2016):

- `SoundManager`: opens the default OpenAL device, keeps a map of
  `SoundSource`s, moves the listener with the camera. `InitAl` runs in
  `RouteEditorGLWidget::surfaceInitialize` when `core.system.soundEnabled`
  is on (off by default); `CloseAl` runs when the window closes.
- `MstsSoundDefinition`: parses `.sms` files (ParserX): scalability groups,
  streams, triggers, volume and frequency curves. Activation and
  deactivation are parsed into empty structs.
- `SoundSource` / `SoundDefinitionGroup::Stream`: one OpenAL source per
  stream, one buffer per trigger's first file; positions relative to the
  listener's tile (`2048 * tile difference`).
- `SoundVariables`: speed, distance, brake cylinder, curve force,
  variables 1-3.
- Users: `Eng` loads the cab `.sms` of the train in play mode
  (`Eng::getCameraPosition`) and drives it from a fake acceleration curve
  (`Eng::updateSim`, `fakesound`); a junction plays a hard-coded `j1.wav`.
  `SoundSourceObj` loads its definition but the source is commented out.
  The editor updates sound on 200 ms and 50 ms ticks
  (`RouteEditorGLWidget`), not every frame.
- Editor objects for sound sources and sound regions exist
  (`PropertiesSoundSource`, `PropertiesSoundRegion`); `Game::soundList` is
  `ssource.dat`.

Known faults (from reading the code, 2026-10-12, not run):

- World sound sources never play (`SoundSourceObj.cpp`, commented out).
- The WAV loader reads the header at fixed offsets: a `fact`, `LIST` or
  `cue ` chunk before `data` breaks it; 8-bit files are uploaded as 16-bit;
  the chunk size is read into an `unsigned short`; lots of `qDebug` per file.
- Frequency curves divide by a fixed 12025 instead of the file's sample
  rate.
- Variable triggers handle only Variable2; random triggers fire once;
  distance-travelled triggers, discrete triggers, `ReleaseLoopReleaseWithJump`,
  enable/disable trigger and stream volume actions are missing.
- `InitAl` failing (no device) does not stop later OpenAL calls; `CloseAl`
  does not check for a context.
- `CMakeLists.txt` hard-codes a Windows OpenAL SDK path
  (`C:/programy/openal-soft-1.18.2-bin`).

Content on the dev server (`~/ORTS`, 2026-10-12): 1712 WAVs, all 16-bit
PCM (924 mono, 788 stereo); one has a `fact` chunk before `data`. Other
content may have 8-bit or compressed files and cue points (step 1 surveys
what the user's content roots hold).

## Decision: miniaudio instead of OpenAL

| | OpenAL Soft | miniaudio |
| --- | --- | --- |
| Dependency | shared library (LGPL), SDK path on Windows | one header in the repo, public domain or MIT-0 |
| Decoding | none (own WAV code) | WAV, FLAC, MP3 built in; Vorbis with stb_vorbis |
| 3D | positions, attenuation, Doppler, HRTF | positions, attenuation models, cones, Doppler; no HRTF |
| Effects | EFX reverb presets, filters | node graph: low/high/band-pass, EQ, delay built in; reverb node in extras; custom nodes for anything else |
| Loop points | `AL_SOFT_loop_points` extension | data source loop points, ranges, custom data sources |
| Tests without a device | not really | null backend; the engine can render without a device |

miniaudio, chosen 2026-10-12: no library to ship or find, its own
decoders, offline rendering for tests on a machine without a sound card, a
licence that fits GPL 3. Missing effects (reverb, occlusion, HRTF via Steam
Audio if ever wanted) are nodes added to the graph later; nothing in the
choice blocks them. Open Rails uses OpenAL, so its behaviour is the
reference for MSTS semantics, not for the backend.

## Design

- **`SoundEngine`** (replaces `SoundManager`): owns the `ma_engine`,
  opens the default device (or none: tests, no device, sound off), master
  volume, listener position and orientation. Every call is a no-op when it
  has no engine, so a missing device never breaks the editor.
- **`SoundBank`**: decoded sounds shared by path (one decode per file per
  run), with their sample rate and cue points.
- **`SoundSource` / streams**: keep the `.sms` model; each playing stream
  owns an `ma_sound` on a shared data source. Volume, pitch and position
  are set from the game thread each frame (miniaudio's sound setters are
  safe to call while the audio thread mixes).
- **Positions**: the listener stays at the origin of the camera's tile as
  today; sources are moved when the camera changes tile.
- **Update**: once per frame with the frame's time step, not on 50/200 ms
  ticks.
- **Vendoring**: `miniaudio.h` of a pinned 0.11 release under
  `src/thirdparty/miniaudio/` (first vendored library: add its licence and
  version to the folder), compiled in one `.c` file with only the decoders
  and backends used.

## Steps

1. **WAV reader** (backend-free): walk RIFF chunks; PCM 8 and 16 bit mono
   and stereo; MS ADPCM and IEEE float if the content survey finds them,
   otherwise reported and skipped; `cue ` / `smpl` loop points. Test suite
   with WAVs built in the test (no binary files in git). Survey all WAVs
   under the content roots for formats and cue use, and record it here.
2. **miniaudio backend**: vendor it, `SoundEngine` and `SoundBank`, port
   the stream code from OpenAL calls, drop OpenAL from CMake (and the
   Windows SDK path). An offline test renders a stream to frames and checks
   level and pitch. Play mode's cab sound plays again.
3. **Trigger and curve semantics** against Open Rails
   (`Orts.Formats.Msts/SoundManagmentFile.cs`,
   `RunActivity/Viewer3D/Sound.cs`, `ALSoundHelper.cs`): all trigger types
   and comparators, all actions including loop release with jump to the
   release cue, random triggers with their delay and volume ranges,
   frequency curves by the file's sample rate, scalability group choice,
   priorities and a voice limit, activation and deactivation (camera type,
   distance). Remove the fake acceleration curve and the `j1.wav` test.
4. **World sounds**: sound source objects (`ssource.dat`), sound regions
   and track sounds (`ttype.dat`, the TDB's sound regions), started and
   stopped by distance.
5. **Train sounds**: external and cab `.sms` for every vehicle in play
   mode, values from the simulation; Doppler.
6. **Environment**: tunnel and cutting reverb (the sky enclosure of task
   renderer 27 can drive the mix), distance low-pass, weather sounds (rain,
   wind) from the weather data (task environment 01).

Step 1 and 2 first; 3-6 in that order unless play-mode testing asks
otherwise.

## Step 1: WAV reader (done)

`WavFile` (`src/tsre/sound/WavFile.h`): RIFF chunks in any order, padded
odd chunks, a data chunk longer than the file cut to what is there; PCM 8
and 16 bit and 32-bit float, also as `WAVE_FORMAT_EXTENSIBLE`, one or two
channels, all read as 16-bit samples. Loop points from the last `cue `
chunk (chunk start + block start + sample offset, as Open Rails) or the last
`smpl` chunk with loops (start and end of each), whichever comes later;
sorted, points past the end dropped. Compressed formats are refused with
the format number. `toMono` keeps the second channel, as Open Rails does
for 3D sounds.

Survey (`--test-suite sound-corpus --test-cases <root>`, 2026-10-12): under
`~/ORTS` 1712 waves, none unreadable, 541 with loop points; 11025 Hz 1431,
22050 Hz 80, 24000 Hz 11, 44100 Hz 190; all 16-bit PCM, so no ADPCM
decoder is needed for now. Loop points follow the data in 541 files, and
`smpl` and `cue ` disagree in some (`SLI.BNSF/SOUND/auto_brake_int.wav`:
the cue chunk comes last and wins).

Stereo loop points count frames: 117 of 120 stereo files with cue points
have them within the frame count (the other 3 past it). Open Rails halves
them when it makes a stereo sound mono for 3D, which moves the loops to
the wrong place; TSRE keeps them.

`sound` suite: 17 cases (formats, chunk order, padding, loop point
precedence, truncated data, refusals, mono).

## Step 2: miniaudio backend (done)

- `src/thirdparty/miniaudio/`: miniaudio 0.11.25 unchanged, its licence and
  a README with the version and checksums. `Miniaudio.cpp` compiles it once
  (no warnings shown) with the options of `MiniaudioConfig.h`: no
  decoders, encoders, generators or resource manager. CMake links threads
  and `dl` instead of OpenAL (and the Windows OpenAL SDK path is gone).
- `SoundEngine`: one per process, the default device or none (tests mix
  with `render()`); the listener is the camera; calls do nothing when it
  is not running.
- `SoundBank` / `SoundClip`: a wave read once per file (and once more as
  mono for 3D), split at its loop points into intro, sustain parts and
  release as Open Rails splits them (fewer than two points: one part).
  `SoundBank::find` looks for a wave in the folders Open Rails searches.
- `StreamPlayer`: a stream's queue of clips in Open Rails' play modes,
  with its queue rules (repeated commands ignored, a waiting loop-release
  of the same clip turned loop, a release dropping loops still waiting,
  out of reach keeping only the newest loop, a released clip without
  volume stopped). Read on the audio thread, sample-accurate: the next
  clip or part follows without a gap, which Open Rails approximates by
  polling OpenAL's buffer queue. The player resamples itself (cubic) from
  each clip's rate with the stream's speed, so clips of different rates
  can follow each other in one stream; miniaudio gets the engine's rate.
- `SoundVoice`: a player as a miniaudio sound, placed in 3D (mono, panned)
  or heard as it is (stereo). The gain with distance is TSRE's (Open
  Rails' model, step 3); miniaudio only pans. Its attenuation model "none"
  turns panning off too, so it gets inverse distance with no rolloff.
- The old stream code (`MstsSoundDefinition`, `SoundManager`) plays
  through voices until step 3 replaces it.

`sound` suite: 43 cases; the player's (exact sample order through intro,
sustain, release and jump, queue rules, speed, clip rates) and offline
renders (pitch and gain of a clip at 11025 Hz, speed, panning, stop).

## Step 3: MSTS sound semantics (done)

Open Rails is the reference for what MSTS sounds do (its code is not
taken); TSRE's own parsers read the files.

- `SmsFile` (`Sms::File`): the .sms grammar on `SimisTextReader` (the
  reader the quadtree code uses): scalability groups, activation and
  deactivation, streams (as many as the count says), the trigger types
  (initial, discrete, variable with every control, random, distance
  travelled, joint/switch/crossover by axles), the commands (play one-shot,
  start loop, start loop release, release, release with jump, enable and
  disable trigger, stream volume), volume and frequency curves with
  granularity, Open Rails' season, weather and time extensions. Numbers
  take unit suffixes by what they measure (distance m, speed m/s, brake
  cylinder psi, force N, power W; "mph", "km", "bar"...), commas between
  numbers are separators, distance thresholds and curve points are squared
  as Open Rails compares them; comment, Skip, `_` and `#` blocks and
  `LoadAllWaves` are skipped. Files are read once (`Sms::shared`).
- `SoundSource`: one sound definition playing for one thing (a car, a place
  in the world, or everywhere), with its own streams and trigger state
  (the old code shared streams between sources, which is why world
  sources were commented out). Per update: within 2 km or out of reach
  (cleared); coming within reach runs the initial triggers and restarts
  loops that should play; activation and deactivation by camera view and
  distance; each stream's triggers; the initial trigger again when nothing
  plays and every variable trigger is below; frequency curve (Hz over the
  playing clip's rate, doubled for mono waves in groups marked Stereo) and
  volume curves; gain by OpenAL's inverse distance clamped model with Open
  Rails' values (full within 8 m, 0.025 at 2 km or at the deactivation
  distance, world sounds rolling off slower at 0.4); a released clip whose
  volume reaches 0 is stopped. Cars set their values (`SoundValues`);
  sounds without a car read the car the camera rides.
- `SoundSystem` (replaces `SoundManager`): the listener (camera, its view,
  the car it rides), the clock random triggers count on, the registry of
  sources, one update per frame from the editor (not 50/200 ms ticks).
- Editor: the play-mode camera rides the activity's lead engine, so the
  listener's view is its cab; the engine's cab .sms is a car source fed
  with the (still faked) speed and Variable2; the junction click stays.
  The old `MstsSoundDefinition`, `SoundManager`, `SoundVariables` and
  `SoundLib` are gone.

Differences from Open Rails, on purpose: sequential selection starts with
the first file (Open Rails starts with the second); stereo loop points are
not halved (step 1); a stream not heard keeps only its newest loop (Open
Rails cleans only streams with discrete triggers). Not yet: joint, switch
and crossover triggers (they need the track under the wheels, step 5), the
player train's longer cutoff, cab pass-through of outside sounds, Open
Rails' season/weather/time extensions (read, not applied).

Content (`sound-corpus`, `~/ORTS`, 2026-10-12): 419 definitions, none
unreadable; triggers: 2397 variable, 1455 discrete, 367 initial, 264
random, 90 distance travelled. 16 have warnings, all content defects:
`TS_COMMON_SOUND/caboose*.sms` close each stream with one `)` too many
(every reader loses all but the first stream), some declare more streams
than they have. 15 have nothing to play (empty `signal.sms` groups,
`gui.sms`). 154 waves are not where any reader looks (trainsets naming
waves of other trainsets' folders; `TS_STARTER_ROUTE` relies on the MSTS
root's SOUND).

`sound` suite: 78 cases (parsing with units, commas, comments and
squared distances; group choice; curves; world and car sources through the
engine without a device: reach, activation and deactivation, initial
trigger again after a release, variable, discrete and random triggers,
frequency, gains).

## Step 4: world sounds (done)

- Route sound sources (`.ws` files' `SoundSource` objects, `SoundSourceObj`):
  each owns a world `SoundSource` for its .sms (the route's SOUND, then the
  root's), made when it loads with sound playing, made again when its file
  changes in the properties, following the object as it moves and silent
  when it is deleted. The heard ones are those within their activation
  distance (and 2 km).
- Track sounds (`TrackSound`): each car of the played consist has the
  outside .sms of its track type from `ttype.dat` (the leading car also the
  inside one, heard from its cab). The leading car takes its type from the
  TDB's sound region items (each names a type) around it: `Ruch` (the
  traveller that moves the train) searches up to 8 km ahead and behind on
  the train's way (`Ruch::distanceToItem`), again once it passes the next
  region (or every 50-200 m); Open Rails' rule picks the type
  (`TrackSound::regionType`). The cars behind change where the leading car
  changed.
- `TSRE_SOUND_LOG=1` logs every 5 s how many sources there are, are heard
  and are playing.

Check in play mode, headless (EUROPE1, `aftstorm.act`, no audio server
reachable, so miniaudio's null device; 2026-10-12): 63-66 sources (route
sources, cab, track sounds), 13-16 heard, about 30 streams playing; the
track type went to 2 at 643 m and back to the default at 855 m.

`sound` suite: 83 cases (the region rule added).

## Step 5: train sounds from the train's motion (done; the rest needs a simulation)

TSRE does not simulate trains yet: play mode moves the activity's consist
along its path (`Ruch`). What a train's sounds can take from motion and
geometry alone is done; what needs a power and brake model is a stand-in.

- Stand-in driver (`PlayDriver`, `src/tsre/trains`): the speed on a sine,
  running half the period and standing the other half (22 m/s, 160 s;
  `TSRE_PLAY_TOP_SPEED`, `TSRE_PLAY_PERIOD`), replacing the constant
  40 m/s; throttle and brake from its rises and falls (a third of the
  throttle to hold the speed, brakes on standing). With a network engine it
  takes the network's speed and derives the same.
- Every car: its outside .sms (the Wagon section's `Sound`, looked for in
  the car's SOUND and the root's SOUND) besides the leading engine's cab
  .sms and the track sounds; heard as far as the train is long and 50 m
  more (at least 2 km). Each car's values: speed, distance run, wheel turns
  (`WheelRadius`), brake cylinder (the brake share of 50 psi), curve force
  and angle of attack in curves (`Ruch::curveRadius` from the TDB's
  sections; curve resistance after Allen 1912 and Astakhov 1966, as Open
  Rails computes it, without superelevation and none standing; smoothed
  over a second).
- Engines: Variable1-3 by engine type as Open Rails fills them, from the
  stand-in throttle: diesel Variable1 the throttle (0-1) and Variable2 the
  engine speed share following it; electric Variable1 the throttle in %
  and Variable2 the pull in % following it; steam Variable1 the driving
  wheels' turning (speed / driving wheel radius / pi x 5, Open Rails'
  formula), Variable2 the cylinder cocks while starting.
- Doppler for sounds placed in 3D: the source's and the listener's
  velocities from their moves (smoothed; jumps are not moves), the speed
  of sound 343.3 m/s.
- Outside sounds heard from a cab or passenger view at half their volume
  (Open Rails' default pass-through).

Not done, needing a train simulation: engine RPM, power, torque and
tractive effort controls, dynamic brake (Variable3), real brake timing,
and the discrete events of cab controls (horn, bell, notches, doors,
couplers...). Joint, switch and crossover triggers are Open Rails
extensions (joints need `ORTSDistanceBetweenTrackJoints` in the route) that
no local content uses; left out.

Check in play mode (EUROPE1, `aftstorm.act`, a steam engine, 2026-10-12):
75 sources; riding the leading engine the speed ran 5 to 22 m/s and back
to standing, Variable1 followed the wheels (35.8 at 22 m/s with 0.975 m
driving wheels), the brake cylinder rose to 50 psi while slowing, curve
force appeared in curves (700-740 N at 21 m/s).

`sound` suite: 91 cases (the driver's profile, curve force, angle of attack,
wheel turns, Doppler, pass-through, the longer reach).

## Verification

- Suites run without a device: WAV reader cases, `.sms` parsing, trigger
  logic, offline renders (level, pitch from frequency curves, loop seams).
- Listening on the dev server through Chrome Remote Desktop (the session's
  audio reaches the client), final judgement on the Steam Deck.
- `core.system.soundEnabled` off (the default) must leave the editor
  unchanged: no device opened, no files read.
- To listen: turn on `core.system.soundEnabled` (Settings, or
  `--set core.system.soundEnabled=true`). In the editor the route's sound
  sources play around the camera; play mode
  (`--play --route EUROPE1 --file aftstorm.act`) adds the lead engine's cab
  sound and the train's track sounds.
