#!/bin/sh
# TSRE's half of the sound comparison: the ET22's drive (drive.py) in a
# capture run, with its sounds logged once a second (TSRE_SOUND_CSV), its
# mix recorded (TSRE_SOUND_RECORD) on miniaudio's null device, the route's
# running noise off (TSRE_MASZYNA_RUNNING_NOISE=vehicle: the ET22's own,
# as MaSzyna plays).
#
#   run.sh OUT GAME_ROOT [ROUTE] [drive.py's options]
#
# TSRE: $TSRE (default: the build next to this checkout). OUT gets
# drive.json, sounds.csv, state.csv, marks.csv, tsre.wav, tsre.log.
set -e
out=$(realpath -m "$1"); root=$2; route=${3:-USA2}
shift 3 2>/dev/null || shift $#
here=$(dirname "$(realpath "$0")")
tsre=${TSRE:-$here/../../../build/TSRE5vc}
mkdir -p "$out"
python3 "$here/drive.py" "$out" "$@"
rm -f "$out/sounds.csv" "$out/state.csv" "$out/marks.csv" "$out/tsre.wav"
TSRE_SOUND_DEVICE=null TSRE_SOUND_RECORD="$out/tsre.wav" TSRE_SOUND_CSV="$out" \
TSRE_MASZYNA_RUNNING_NOISE=vehicle TSRE_SOUND_LOG=1 \
    timeout 1800 "$tsre" --game-root "$root" --route "$route" --set core.system.soundEnabled=true \
    --test --test-suite renderer-capture --test-cases "$out/drive.json" > "$out/tsre.log" 2>&1
grep -a "mark\|Sound: recorded" "$out/tsre.log" || true
