# sound-compare

The same drive of the ET22 in TSRE and in MaSzyna, each one's sounds logged
once a second and its mix recorded, to compare how loud each sound plays
(task sound 03, the user's review of the ET22's cab, 2026-11-14).

The drive: start in the cab with everything off, then the battery, both
pantographs, the main switch, the converter and the compressor. When the
main reservoir is full, release the brakes, notch up every 2 seconds to
100 km/h, coast for 10 s, apply service braking until stopped, and wait 20 s.
The vigilance device is acknowledged every 30 s.

- `tsre/`: TSRE's half.
  - `drive.py` writes the drive as a renderer-capture timeline.
  - `run.sh OUT GAME_ROOT [ROUTE]` runs it, with the route's running noise
    off (`TSRE_MASZYNA_RUNNING_NOISE=vehicle`), so the ET22 plays only its own
    `.mmd` sounds, as in MaSzyna.
  - The activity must have the ET22 as the player's train (default
    `evegrain.act`, train 1).
- `maszyna/`: MaSzyna's half: a log patch for eu07 (built from a copy of
  MaSzyna's source), a test scenery, and the driver. See its README.
- `compare.py TSRE_RUN MASZYNA_RUN [--out report.md]`: each sound's level by
  phase of the drive and by speed, side by side.

## Each run's files

- **`sounds.csv`**: `t,key,file,x,y,z,gain,pitch,soundproofing,distance,final`.
  - One row per playing voice each second.
  - `gain` is what the vehicle's code set.
  - `final` is what the voice plays at, after the soundproofing and the
    distance, before the master volume.
- **`state.csv`**: TSRE writes it as `t,name,value` rows (every value the
  loco script publishes, plus `speedKmh`, `simTime` and `window`). MaSzyna's
  is one row per second with named columns.
- **`marks.csv`**: `t,...,event`, what the driver did and when, on the
  sounds' clock.
- **The recording**: `tsre.wav` or `maszyna.wav`.

## TSRE's switches (any TSRE session, also on a sound card)

- `TSRE_SOUND_CSV=<folder>`: the three CSV files above.
- `TSRE_SOUND_RECORD=<file.wav>`: the mix as played, as a 32-bit float WAV.
  The log reports its peak and how many samples went over full scale, which
  would clip on a sound card.
- `TSRE_SOUND_DEVICE=null`: played to miniaudio's null device. It is paced
  like a sound card and nobody hears it (recording on a machine without
  one). `none`: nothing is played at all.
