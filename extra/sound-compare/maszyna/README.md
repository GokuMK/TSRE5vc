# Sound comparison: the MaSzyna side

MaSzyna (eu07) plays the ET22's `.mmd` sounds during a scripted drive and logs, once per simulated
second, every OpenAL source it is playing, with the volumes MaSzyna computed for it. TSRE logs the same
drive in the same format, so the two can be compared sound by sound.

`sound-log.patch` changes MaSzyna's source files, which are under the Mozilla Public License 2.0. The
patch is under MPL-2.0 like them. The checkout itself is never modified: the patch is applied to a copy.

## Running

```
WORK=/tmp/eu07-sound-compare extra/sound-compare/maszyna/run.sh <output dir>
```

One command: builds eu07 when needed, prepares a run directory, starts Xvfb, openbox and eu07, runs
`drive.py` and collects the outputs. A run takes about 5 minutes (255 s of it real time in the
simulation), the first build about 15.

Needs: the MaSzyna source checkout at commit 66a940d4 (`MASZYNA_SRC`, read only), the MaSzyna game data
(`MASZYNA_CONTENT`, read only), cmake, ninja, a C++20 compiler, the system's glfw/OpenAL Soft/libsndfile/
luajit/libpng/curl, Xvfb, openbox, xdotool, xset, python3, and network access for the first build.
bubblewrap (`bwrap`) is used when present to mount the game data read only for eu07.

Environment: `WORK` (source copy, build, run directory), `MASZYNA_SRC`, `MASZYNA_CONTENT`,
`DISPLAY_NUM` (Xvfb display, default 91), `JOBS`, `SEED` (eu07's `-seed`, default `tsre`; it picks the
`.mmd`'s random includes, so the same seed gives the same horn and converter/compressor set),
`RENDER` (`none`: the scene is not drawn, `gfx.skiprendering`, about 170 fps on llvmpipe; `legacy`: the
fixed-function renderer, about 12 fps; `full`: the shader renderer with the heavy effects off, about 7 fps;
default `none`: the sound code runs per frame, so the higher frame rate gives the smoother mix and nothing
of the sound depends on drawing), `REBUILD=1`, `BUILD_ONLY=1`.

### Build

- The checkout is copied to `$WORK/eu07-src` without `.git` (rsync keeps the file times, so a changed
  patch rebuilds only what it touches). Nothing is written into the checkout.
- The submodules the build needs and the checkout leaves empty (glfw, glm, nlohmann json, asio,
  yaml-cpp) are downloaded from GitHub at the commits the checkout pins (`git ls-tree`).
- `sound-log.patch` is applied with `patch -p1` (by hand: `patch -d <copy> -p1 < sound-log.patch`).
  The patch is the source of truth; run.sh rebuilds when it differs from the one last applied.
- Release (-O3), `WITH_PYTHON=OFF WITH_UART=OFF WITH_DISCORD_RPC=OFF WITH_TESTS=OFF WITH_OPENVR=OFF
  WITH_CRASHPAD=OFF ADD_VERSION_SUFFIX=OFF`. The binary is `$WORK/eu07-build/bin/eu07`.
- The patch also fixes `scripting/PyIntStub.cpp` (a `const` missing at 66a940d4), without which
  `WITH_PYTHON=OFF` does not build.

### Run directory

`$WORK/run` mirrors the game data: each top-level entry is a symlink, except `eu07.ini` and the
`eu07_input-*.ini` files (copies), `scenery/` (a real directory of symlinks to each scenery file plus
`tsre_sound_compare.scn`; eu07 writes scenery caches there) and `screenshots/`. eu07 runs with `HOME`
inside the run directory, so `~/.config/MaSzyna/eu07.ini` is not read. The settings run.sh appends to the
copied `eu07.ini` (they override the shipped ones): 640x400 window, no vsync, no shadows, environment map,
motion blur or smoke, multisampling off, the test scenery and vehicle, and the `RENDER` choice. Sound
settings stay as shipped (`sound.volume 1.5`, the vehicle/positional/ambient volumes 1).

OpenAL Soft writes what it mixes to `<output dir>/maszyna.wav` (`ALSOFT_CONF` pointing to the run
directory's `alsoft.conf` with `drivers=wave`, plus `ALSOFT_DRIVERS=wave`). eu07 asks for the default
device, which is then the wave writer.

## The scenery

`scenery/tsre_sound_compare.scn`: a straight 6 km track along +z (240 tracks of 25 m with Calkowo v2's
main line definition: `track normal 25.0 1.435 0.25 25.0 20 0 Flat vis`, `rail_screw_used1 4 tpbps-new2`,
25 m joints, 140 km/h) under a catenary (Calkowo v2's span definition, 50 m spans, contact wire 5.6 m
above the track, one 3600 V power source like Calkowo's: the pantographs get about 3600 V, which also sets
the converter's pitch, voltage / nominal voltage, about 1.2), July 15 (`config movelight 196`), 12:00,
overcast 0 (dry), no signals, no other trains. The player's train is `ET22-1036`:
`dynamic pkp\et22_v3 201e-1036 201e`, light engine, standing (velocity 0), so MaSzyna starts it cold and
braked: battery off, pantographs down, brake pipe empty, main reservoir 4.1 bar. The camera is in cab 1 at
`driver1sitpos` (the driver's seat), windows shut.

## The drive (drive.py)

xdotool key presses into the eu07 window (keys from the run directory's `eu07_input-keyboard.ini`, each
held 0.2-0.5 s so that impulse switches see it; key auto-repeat is off), conditions read from
`state.csv`. Each action writes a mark to `marks.csv` (`t,event`, t of the latest `state.csv` row):

| mark | action |
|---|---|
| `start` | first state row; 10 s wait |
| `battery` | `batterytoggle` (j); 2 s |
| `reverser`, `ack`, `relay_reset` | `reverserincrease` (d), `alerteracknowledge` (space), `motoroverloadrelayreset` (n); 3 s. MaSzyna takes the acknowledgement of the cab signal alarm that the battery starts, and the ground relay reset the line breaker needs, only with the reverser out of neutral |
| `pantographs`, `pantographs_up` | `pantographtogglefront` (p), `pantographtogglerear` (o); until both are raised (the mover's flags), then 8 s: they reach the wire about 6 s after the keys |
| `main_switch` (`main_switch_retry`) | `linebreakertoggle` (m) held 1.5 s: it closes on release after `IniCDelay`; up to 3 tries; 5 s |
| `converter` | `convertertoggle` (x); 5 s |
| `compressor`, `main_res_7bar` | `compressortoggle` (c); until the main reservoir has 7 bar, max 240 s |
| `release`, `releaser`, `brakes_released` | `trainbrakerelease` (num 6), `independentbrakedecreasefast` (shift num 7) x3; the releaser `independentbrakebailoff` (num 4) held until the brake pipe has 3 bar (MaSzyna's FV4a does not charge a pipe below 2.75 bar without it); until the brake cylinder is below 0.2 bar and the pipe at 4.8 bar |
| `power` | `mastercontrollerincrease` (num +) every 2 s through the 48 positions; when the master controller does not move, `secondcontrollerincrease` (num /, the shunting steps) |
| `100kmh`, `power_off` | at 100 km/h the shunting steps, then the master controller back to 0 (`...decreasefast`) |
| (10 s coasting) | |
| `brake`, `stopped` | `trainbrakeservice` (num 5, FV4a full service: brake pipe 5 to 3.5 bar); until below 0.5 km/h |
| `end` | 20 s later; eu07 is closed (WM_DELETE_WINDOW) |
| `ack` | `alerteracknowledge` (space) every 30 s from the battery on (ET22: `AwareDelay=60`, counted above 12.5 km/h) |

Overload handling: when the line breaker drops while powering, or the current stays 0 with the
controller up and the train moving (overload relay), both controllers go back to 0, the relays are reset
(n), the breaker is closed again when needed and the notching slowed by 2 s (`main_switch_tripped`,
`main_switch_reclosed`, `overload_tripped`, `overload_reset`). A brake pipe below 3 bar before `brake` is
taken as an emergency brake application: `emergency_brake` is marked and drive.py stops with an error.

## Outputs

### sounds.csv

`#` lines first: the commit, the vehicle, the volumes from eu07.ini that `final` does not include, the
conventions, and where t=0 is in `maszyna.wav` (seconds after the audio device opened, which is where the
wave writer starts; simulated time keeps pace with real time, see log.txt). Then one row per playing
OpenAL source instance at each sample (t = 1, 2, 3 ... simulated seconds since the scenario became ready):

| column | meaning |
|---|---|
| `t` | simulated seconds |
| `key` | the `.mmd` key the sound was read from (`tractionmotor`, `converter`, `runningnoise`, ...), for cab controls the cab key (`mainctrl`, `brakectrl`, ...); `scenery:<name>` for scenery sounds; built-in defaults carry their sample name (`rainsound_default`) |
| `file` | the sample playing now, as named in the `.mmd` (lower case, `/`): for a sample table the chunk's file, for a multipart sound the begin, main or end part |
| `x,y,z` | the source's offset in its vehicle's frame, metres: x left, y up, z to the vehicle's front (`sound_source::offset()`, used as `GetPosition() + VectorLeft()*x + VectorUp()*y + VectorFront()*z`) |
| `gain` | the volume the vehicle/cab code set (`sound_source::gain()`, clamped 0..2), before table crossfade, soundproofing and distance |
| `pitch` | `AL_PITCH` as OpenAL has it (the code's pitch times the emitter's random pitch variation, clamped 0.1..10); OpenAL's Doppler shift comes on top |
| `soundproofing` | the soundproofing factor for the listener's place (cab, window, outside), from the `.mmd` table or the sound's own `soundproofing:` |
| `distance` | OpenAL's distance factor at the listener, computed with `AL_INVERSE_DISTANCE_CLAMPED` from the source's `AL_REFERENCE_DISTANCE` (range/16 * soundproofing; 5/16 for range < 0), `AL_ROLLOFF_FACTOR` (1.75), `AL_MAX_DISTANCE` and the source-listener distance |
| `final` | `AL_GAIN` / category volume * `distance` = gain * crossfade * soundproofing * fade beyond the nominal range * distance; excludes `sound.volume` (listener gain) and the category volume |

All sound buffers are mono (eu07 mixes stereo files down), so OpenAL attenuates and pans every source.
OpenAL Soft clamps (`final` * category volume) to `AL_MAX_GAIN` (1.0) per source before the listener gain.

### state.csv

One row per sample for the player's vehicle:
`t,speed_kmh,mc,sc,current_a,motor_rps,converter,compressor,main_switch,battery,panto_front,panto_rear,brake_cyl_bar,pipe_bar,main_res_bar,window`
from the mover: `Vel`, `MainCtrlPos`, `ScndCtrlPos`, `Im`, `enrot`, `ConverterFlag`, `CompressorFlag`,
`Mains`, `Battery`, `Pantographs[front/rear].is_active` (the pantograph's valve is open: it starts to rise),
`BrakePress`, `PipePress`, `Compressor` (the mover keeps pressures in bar already: `MaxBP=4.2`,
`MinCP=7.5`) and `Global.CabWindowOpen`. Flushed every second.

### log.txt

Every 10 simulated seconds the patch writes the pace: `sound log: simulated <s> s in <s> s real time,
<n> fps` (the frame rate over the last 10 s).

### marks.csv, maszyna.wav, logs

`marks.csv` as above; `maszyna.wav` the mix OpenAL Soft wrote, in real time from the device opening to
eu07's exit (48 kHz, stereo, 32-bit float, so peaks above 0 dBFS are kept, not clipped); `log.txt`,
`errors.txt` (eu07), `eu07.out`, `drive.log`.

## Caveats

- The drive differs from a "battery, pantographs, line breaker, converter, compressor, release, reverser"
  order in one place: MaSzyna takes the acknowledgement of the cab signal alarm that the battery starts,
  and the ground relay reset without which the line breaker does not close, only with the reverser out of
  neutral, so the reverser goes forward 2 s after the battery (mark `reverser`).
- The cold ET22 starts with an empty brake pipe and MaSzyna's FV4a does not charge it below 2.75 bar unless
  the releaser is held (`releaser` mark): the releaser's and the brake valve's sounds are part of the drive.
- The cold start plays the end part of the `emergencybrake` sound at t=1, before any key.
- The vigilance acknowledgement every 30 s and each controller step play their cab control sounds
  (`security_reset_bt`, `mainctrl`, `scndctrl`).
- `.mmd` keys read by both the vehicle and the cab parsers keep their own key: `brakesound` is the
  vehicle's sample table (external, soundproofing 0 in the cab) and the cab's `697_brake.wav`.
- The random includes of the `.mmd` (horn set, converter/compressor set) depend on `SEED`; with `tsre`
  the converter/compressor set is `converter_compressor_set1.inc` (`697_et22_converter-*.wav`,
  `697_et22_compressor-*.wav`).
- With `RENDER=none` nothing is drawn; MaSzyna's sound code does not depend on drawing (vehicle and cab
  sounds are updated from the simulation and the camera, not from the renderer), but screenshots are
  black apart from the UI. Use `RENDER=legacy` to watch.
