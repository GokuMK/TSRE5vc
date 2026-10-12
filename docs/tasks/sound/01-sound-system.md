# Task Sound 01 - Sound System

Status: planned (2026-10-12), branch `feature/sound`. Nothing implemented yet.

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

## Verification

- Suites run without a device: WAV reader cases, `.sms` parsing, trigger
  logic, offline renders (level, pitch from frequency curves, loop seams).
- Listening on the dev server through Chrome Remote Desktop (the session's
  audio reaches the client), final judgement on the Steam Deck.
- `core.system.soundEnabled` off (the default) must leave the editor
  unchanged: no device opened, no files read.
