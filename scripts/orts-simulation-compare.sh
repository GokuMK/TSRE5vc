#!/usr/bin/env bash
# Runs an activity in Open Rails for comparison with TSRE's simulation (task
# simulation 02, phase 7): the player's train on Open Rails' autopilot, the
# game sped up, and every two seconds the time of day and the dispatcher
# page (every train's state) read from Open Rails' web server.
# Usage: scripts/orts-simulation-compare.sh <game-root> <route> <work-dir> <activity> <until hh:mm> [speed presses]
#
# Makes <work-dir>/MSTS a view of the game root (symlinks: Open Rails writes
# nothing there), gives Open Rails its own settings in <work-dir>/config
# (its user data, saves and the player's station stop log, *Stops.csv, go
# there too), runs it on its own display with a window manager (Open Rails
# reads keys only in a focused window), and writes the samples to
# <work-dir>/logs/<activity>.jsonl. Speed presses: Ctrl+Alt+PageUp, 1.5
# times each (default 3). scripts/orts-dispatcher-stops.py reads the
# samples.
#
# ORTS_PROGRAM: the folder with RunActivity (default: the sibling
# openrails-linux-spike worktree's Program). DOTNET_ROOT: the .NET install
# (default ~/.local/dotnet when it exists). ORTS_WINDOW: its window size
# (default 640x400; smaller draws faster on software GL, so the game runs
# nearer the speed asked). Needs xvfb-run, xdotool, openbox and curl.
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
    while kill -0 "$pid" 2>/dev/null; do
        time="$(now)"
        # One line a sample: the time and the dispatcher table.
        curl -s -m 10 http://localhost:2150/API/HUD/7 | python3 -c '
import json, sys
try:
    table = json.load(sys.stdin)["extraTable"]
except Exception:
    sys.exit(0)
print(json.dumps({"time": float(sys.argv[1]), "hud": {"extraTable": table}}))' "$time" >> "$samples" || true
        python3 -c "import sys; sys.exit(0 if float('$time') >= $until else 1)" && break
        sleep 2
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
presses="${6:-3}"
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
WindowSize=string:${ORTS_WINDOW:-640x400}
ViewingDistance=int:500
DynamicShadows=bool:false
EOF

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
