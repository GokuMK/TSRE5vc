#!/usr/bin/env bash
# Runs an activity in Open Rails for comparison with TSRE's simulation (task
# simulation 02, phase 7): the player's train on Open Rails' autopilot, the
# game sped up, and every second or so (ORTS_SAMPLE seconds) the time of day
# and the dispatcher page (every train's state) read from Open Rails' web
# server.
# Usage: scripts/orts-simulation-compare.sh <game-root> <route> <work-dir> <activity> <until hh:mm> [speed presses]
#
# Makes <work-dir>/MSTS a view of the game root (symlinks: Open Rails writes
# nothing there), gives Open Rails its own settings in <work-dir>/config
# (its user data, saves and the player's station stop log, *Stops.csv, go
# there too), runs it on its own display with a window manager (Open Rails
# reads keys only in a focused window), and writes the samples to
# <work-dir>/logs/<activity>.jsonl. Speed presses: Ctrl+Alt+PageUp, 1.5
# times each (default 7: 17 times real time, which the game keeps on the
# llvmpipe box and which gives Open Rails' stops and waits within a sample
# of a run at 5 times; at 38 times they come up to a minute later).
# scripts/orts-dispatcher-stops.py reads the samples.
#
# ORTS_PROGRAM: the folder with RunActivity (default: the sibling
# openrails-linux-spike worktree's Program). DOTNET_ROOT: the .NET install
# (default ~/.local/dotnet when it exists). ORTS_WINDOW: its window size
# (default 400x250). ORTS_LEAN: 0 keeps Open Rails' own drawing and sound
# settings; else less is drawn (no vertical sync, distant mountains,
# overhead wire, extended LOD; scenery objects of detail level 0 only) and
# no sound played, which doubles the frame rate there (frames of 25 to 45
# ms). Faster frames mean smaller steps for Open Rails' AI at a given game
# speed, and a frame over 250 ms would simulate nothing. Needs xvfb-run,
# xdotool, openbox and curl.
set -euo pipefail

if [[ "${1:-}" == --inner ]]; then
    # On the virtual display: Open Rails, then the keys, then the samples.
    program="$2"; activity="$3"; logs="$4"; until="$5"; presses="$6"; samples="$7"
    openbox > "$logs/openbox.out" 2>&1 &
    cd "$program"
    ./RunActivity -start -activity "$activity" > "$logs/run.out" 2>&1 &
    pid=$!
    for _ in $(seq 1 300); do
        sleep 2
        kill -0 "$pid" 2>/dev/null || { echo "Open Rails exited while loading"; exit 1; }
        grep -q "State(GameStateViewer3D)" "$logs/OpenRailsLog.txt" 2>/dev/null && break
    done
    sleep 5
    window="$(xdotool search --name "Open Rails" | head -1)"
    # Keys reach it only while it is the active window with the pointer in
    # it, and only held across its frames.
    focus() {
        xdotool windowactivate --sync "$window" 2>/dev/null || true
        xdotool mousemove --window "$window" 100 100
        sleep 1
    }
    press() {
        local keys=("$@") key
        for key in "${keys[@]}"; do xdotool keydown "$key"; done
        sleep 0.3
        for ((i = ${#keys[@]} - 1; i >= 0; i--)); do xdotool keyup "${keys[i]}"; done
        sleep 0.5
    }
    now() { curl -s -m 5 http://localhost:2150/API/TIME || echo 0; }
    # It starts paused: Escape (each opens or closes the pause menu) until
    # the clock runs.
    for _ in $(seq 1 6); do
        before="$(now)"; sleep 3
        [[ "$(now)" != "$before" ]] && break
        focus; press Escape
    done
    # Autopilot (Alt+A) until the player's train is driven by Open Rails.
    for _ in 1 2 3 4 5; do
        focus; press Alt_L a; sleep 3
        curl -s -m 10 http://localhost:2150/API/HUD/7 | python3 -c '
import json, sys
t = json.load(sys.stdin)["extraTable"]
v, c = t["values"], t["nCols"]
for r in range(t["nRows"]):
    row = v[r * c:(r + 1) * c]
    row = [(cell or "").strip() for cell in row]
    if row[0].startswith("0 "):
        sys.exit(0 if len(row) > 4 and row[4] else 1)
sys.exit(1)' && break
    done
    focus
    for _ in $(seq 1 "$presses"); do press Control_L Alt_L Page_Up; done
    : > "$samples"
    debug="${samples%.jsonl}-debug.txt"
    : > "$debug"
    count=0
    still=0
    last=""
    while kill -0 "$pid" 2>/dev/null; do
        time="$(now)"
        # Its clock stopped (the activity over, a window open): done.
        if [[ "$time" == "$last" ]]; then
            (( ++still >= 30 )) && { echo "the game clock stopped at $time"; break; }
        else
            still=0
        fi
        last="$time"
        # One line a sample: the time of day, the wall clock and the
        # dispatcher table.
        curl -s -m 10 http://localhost:2150/API/HUD/7 | python3 -c '
import json, sys
try:
    table = json.load(sys.stdin)["extraTable"]
except Exception:
    sys.exit(0)
print(json.dumps({"time": float(sys.argv[1]), "wall": float(sys.argv[2]), "hud": {"extraTable": table}}))' \
            "$time" "$(date +%s.%N)" >> "$samples" || true
        # Every thirty samples the debug page: frame times, the processes'
        # loads.
        if (( count++ % 30 == 0 )); then
            curl -s -m 10 http://localhost:2150/API/HUD/9 | python3 -c '
import json, sys
try:
    table = json.load(sys.stdin)["extraTable"]
except Exception:
    sys.exit(0)
values, columns = table["values"], table["nCols"]
keep = ("GPU", "Render process", "Updater process", "Loader process", "Sound process", "Render primitives")
for r in range(table["nRows"]):
    row = [(cell or "").strip() for cell in values[r * columns:(r + 1) * columns]]
    if row and row[0] in keep:
        print(sys.argv[1], row[0], " ".join(cell for cell in row[1:] if cell))' "$time" >> "$debug" || true
        fi
        python3 -c "import sys; sys.exit(0 if float('$time') >= $until else 1)" && break
        sleep "${ORTS_SAMPLE:-1}"
    done
    kill "$pid" 2>/dev/null || true
    exit 0
fi

if [[ $# -lt 5 ]]; then
    echo "usage: $0 <game-root> <route> <work-dir> <activity> <until hh:mm> [speed presses]" >&2
    exit 2
fi
game_root="$(cd "$1" && pwd)"
route="$2"
work="$(mkdir -p "$3" && cd "$3" && pwd)"
activity="$4"
until="$(python3 -c "import sys; h, m = sys.argv[1].split(':'); print(int(h) * 3600 + int(m) * 60)" "$5")"
presses="${6:-7}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
program="${ORTS_PROGRAM:-$repo/../openrails-linux-spike/Program}"
if [[ -z "${DOTNET_ROOT:-}" && -d "$HOME/.local/dotnet" ]]; then
    export DOTNET_ROOT="$HOME/.local/dotnet"
fi
[[ -x "$program/RunActivity" ]] || { echo "no RunActivity in $program (set ORTS_PROGRAM)" >&2; exit 2; }

# The view of the game root: made once, symlinks only.
view="$work/MSTS"
if [[ ! -e "$view" ]]; then
    mkdir -p "$view/ROUTES"
    for entry in "$game_root"/*; do
        name="$(basename "$entry")"
        [[ "$name" == ROUTES ]] || ln -s "$entry" "$view/$name"
    done
    touch "$view/TSRE_SCRATCH"
fi
[[ -e "$view/TSRE_SCRATCH" ]] || { echo "$view was not made by this script" >&2; exit 2; }
[[ -e "$view/ROUTES/$route" ]] || ln -s "$game_root/ROUTES/$route" "$view/ROUTES/$route"
act="$(cd "$view/ROUTES/$route/ACTIVITIES" && ls | grep -i -x "$activity.act" | head -1)"
[[ -n "$act" ]] || { echo "no activity $activity in $route" >&2; exit 2; }

logs="$work/logs"
mkdir -p "$logs" "$work/config/Open Rails"
encoded="$(python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=""))' "$logs")"
cat > "$work/config/Open Rails/OpenRails.ini" <<EOF
[ORTS]
LoggingPath=string:$encoded
ScreenshotPath=string:$encoded
FullScreen=bool:false
AntiAliasing=int:1
ShowErrorDialogs=bool:false
DataLogStationStops=bool:true
WindowSize=string:${ORTS_WINDOW:-400x250}
ViewingDistance=int:500
DynamicShadows=bool:false
EOF
# Less to draw and to play (ORTS_LEAN=0 leaves Open Rails' own values).
if [[ "${ORTS_LEAN:-1}" != 0 ]]; then
    cat >> "$work/config/Open Rails/OpenRails.ini" <<EOF
VerticalSync=bool:false
DistantMountains=bool:false
Wire=bool:false
LODViewingExtension=bool:false
WorldObjectDensity=int:0
SoundDetailLevel=int:0
SoundVolumePercent=int:0
EOF
fi

samples="$logs/${activity}.jsonl"
# Its own process group: the subshell becomes setsid, then xvfb-run.
(XDG_CONFIG_HOME="$work/config" exec setsid xvfb-run -a -s "-screen 0 800x600x24" \
    "$repo/scripts/orts-simulation-compare.sh" --inner "$program" "$view/ROUTES/$route/ACTIVITIES/$act" \
    "$logs" "$until" "$presses" "$samples" > "$logs/${activity}.out" 2>&1) &
runner=$!
wait "$runner" || true
kill -- "-$runner" 2>/dev/null || true
cp "$logs/OpenRailsLog.txt" "$logs/${activity}.log" 2>/dev/null || true
echo "$(wc -l < "$samples") samples in $samples"
# How fast the game ran against the wall clock.
python3 - "$samples" <<'PY'
import json, sys
samples = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
timed = [s for s in samples if "wall" in s]
if len(timed) >= 2 and timed[-1]["wall"] > timed[0]["wall"]:
    game = timed[-1]["time"] - timed[0]["time"]
    wall = timed[-1]["wall"] - timed[0]["wall"]
    print("game time %.0f s in %.0f s: %.2f times real time" % (game, wall, game / wall))
PY
tail -n 6 "${samples%.jsonl}-debug.txt" 2>/dev/null || true
