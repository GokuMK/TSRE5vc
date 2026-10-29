#!/usr/bin/env bash
# Drives an activity's player train in Open Rails by a list of timed
# commands, for comparison with TSRE's driving (task driving 02, step 5):
# the same commands TSRE's simulation-route suite takes with
# TSRE_SIM_DRIVE ("seconds name value" a line, seconds from the activity's
# start; throttle, trainBrake, engineBrake, dynamicBrake 0 to 1, direction
# -1/0/1), posted to Open Rails' web server (/API/CABCONTROLS) at those game
# times, and the driving display (/API/TRAINDRIVINGDISPLAY) read every half
# second of wall time into <work-dir>/logs/<activity>-drive.jsonl, with the
# HUD pages ORTS_HUD_PAGES names ("2 6": the locomotives, the forces).
# scripts/drive-compare.py sets the two side by side.
# Usage: scripts/orts-drive-run.sh <game-root> <route> <work-dir> <activity> <commands file>
#
# As scripts/orts-simulation-compare.sh: <work-dir>/MSTS a view of the game
# root, Open Rails' own settings in <work-dir>/config (metric units), its
# own display with a window manager, the game at real time. ORTS_PROGRAM,
# DOTNET_ROOT, ORTS_WINDOW and ORTS_LEAN as there. Needs xvfb-run, xdotool,
# openbox and curl.
set -euo pipefail

if [[ "${1:-}" == --inner ]]; then
    program="$2"; activity="$3"; logs="$4"; commands="$5"; samples="$6"
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
    # The API's answers once, to see their form.
    curl -s -m 10 "http://localhost:2150/API/TRAINDRIVINGDISPLAY?normalText=true" > "$logs/driving-display.json" || true
    curl -s -m 10 http://localhost:2150/API/CABCONTROLS > "$logs/cab-controls.json" || true
    # It starts paused: Escape until the clock runs.
    for _ in $(seq 1 6); do
        before="$(now)"; sleep 3
        [[ "$(now)" != "$before" ]] && break
        focus; press Escape
    done
    start="$(now)"
    python3 - "$commands" "$samples" "$start" "$pid" <<'PYTHON'
import json, os, subprocess, sys, time, urllib.request

commands_file, samples_file, start, pid = sys.argv[1], sys.argv[2], float(sys.argv[3]), int(sys.argv[4])
types = {"throttle": "THROTTLE", "trainBrake": "TRAIN_BRAKE", "engineBrake": "ENGINE_BRAKE",
         "dynamicBrake": "DYNAMIC_BRAKE", "direction": "DIRECTION"}
commands = []
for line in open(commands_file):
    words = line.split()
    if len(words) >= 3 and not words[0].startswith("#") and words[1] in types:
        commands.append((float(words[0]), types[words[1]], float(words[2])))
end = max(c[0] for c in commands)
hud_pages = [int(p) for p in os.environ.get("ORTS_HUD_PAGES", "").split()]

def get(path):
    with urllib.request.urlopen("http://localhost:2150/API/" + path, timeout=5) as r:
        return r.read().decode("utf-8", "replace")

last = {}

def post(type_name, value):
    # Open Rails takes a control's value only when it changes (from 0 at
    # first): the same value again goes a hair beside it first.
    values = [value]
    if last.get(type_name, 0.0) == value:
        values = [value + 1e-4, value]
    for v in values:
        data = json.dumps([{"TypeName": type_name, "ControlIndex": 0, "Value": v}]).encode()
        request = urllib.request.Request("http://localhost:2150/API/CABCONTROLS", data=data,
                                         headers={"Content-Type": "application/json"}, method="POST")
        urllib.request.urlopen(request, timeout=5).read()
        if len(values) > 1:
            time.sleep(0.3)
    last[type_name] = value

sent = 0
with open(samples_file, "w") as out:
    while True:
        try:
            os.kill(pid, 0)
        except OSError:
            break
        try:
            game = float(get("TIME")) - start
        except Exception:
            time.sleep(0.5)
            continue
        while sent < len(commands) and commands[sent][0] <= game:
            post(commands[sent][1], commands[sent][2])
            sent += 1
        try:
            display = json.loads(get("TRAINDRIVINGDISPLAY?normalText=true"))
        except Exception:
            display = None
        hud = {}
        for page in hud_pages:
            try:
                hud[page] = json.loads(get("HUD/%d" % page))
            except Exception:
                pass
        out.write(json.dumps({"seconds": game, "wall": time.time(), "display": display, "hud": hud}) + "\n")
        out.flush()
        if game > end:
            break
        time.sleep(0.5)
PYTHON
    kill "$pid" 2>/dev/null || true
    exit 0
fi

if [[ $# -lt 5 ]]; then
    echo "usage: $0 <game-root> <route> <work-dir> <activity> <commands file>" >&2
    exit 2
fi
game_root="$(cd "$1" && pwd)"
route="$2"
work="$(mkdir -p "$3" && cd "$3" && pwd)"
activity="$4"
commands="$(cd "$(dirname "$5")" && pwd)/$(basename "$5")"
repo="$(cd "$(dirname "$0")/.." && pwd)"
program="${ORTS_PROGRAM:-$repo/../openrails-linux-spike/Program}"
if [[ -z "${DOTNET_ROOT:-}" && -d "$HOME/.local/dotnet" ]]; then
    export DOTNET_ROOT="$HOME/.local/dotnet"
fi
[[ -x "$program/RunActivity" ]] || { echo "no RunActivity in $program (set ORTS_PROGRAM)" >&2; exit 2; }

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
WindowSize=string:${ORTS_WINDOW:-400x250}
ViewingDistance=int:500
DynamicShadows=bool:false
Units=string:Metric
PressureUnit=string:PSI
EOF
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

samples="$logs/${activity}-drive.jsonl"
# Its own process group: the subshell becomes setsid, then xvfb-run.
(XDG_CONFIG_HOME="$work/config" exec setsid xvfb-run -a -s "-screen 0 800x600x24" \
    "$repo/scripts/orts-drive-run.sh" --inner "$program" "$view/ROUTES/$route/ACTIVITIES/$act" \
    "$logs" "$commands" "$samples" > "$logs/${activity}-drive.out" 2>&1) &
runner=$!
wait "$runner" || true
kill -- "-$runner" 2>/dev/null || true
cp "$logs/OpenRailsLog.txt" "$logs/${activity}-drive.log" 2>/dev/null || true
echo "$(wc -l < "$samples") samples in $samples"
