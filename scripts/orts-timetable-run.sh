#!/usr/bin/env bash
# Runs an Open Rails timetable in Open Rails (task simulation 08, phases 6
# and 7): Open Rails reads it and pre-runs its AI trains up to the player's
# train's start, then the player's train runs on Open Rails' autopilot with
# the game sped up, and every second or so (ORTS_SAMPLE seconds) the time of
# day and the dispatcher page (every train's state) are read from Open
# Rails' web server, as scripts/orts-simulation-compare.sh does for
# activities.
# Usage: scripts/orts-timetable-run.sh <game-root> <route> <work-dir> <timetable file> <player train> <until hh:mm|load> [speed presses]
#
# <timetable file>: a file in the route's ACTIVITIES/OPENRAILS, or any path
# (it is linked into the view's ACTIVITIES/OPENRAILS: the route's own files
# are not touched). <player train>: its name in the timetable (Open Rails
# names it "<description>:<name>", found here). "load" as <until>: only
# until Open Rails has read the timetable and shows the first frame (its
# log then holds what it found in the timetable).
#
# Makes <work-dir>/MSTS a view of the game root (symlinks; the route a real
# folder of symlinks with its own ACTIVITIES/OPENRAILS and PATHS), gives Open Rails
# its own settings in <work-dir>/config, runs it on its own display with a
# window manager, and writes the samples to <work-dir>/logs/<train>.jsonl
# and its log to <work-dir>/logs/OpenRailsLog.txt. ORTS_PROGRAM,
# DOTNET_ROOT, ORTS_WINDOW, ORTS_LEAN as in orts-simulation-compare.sh.
set -euo pipefail

if [[ "${1:-}" == --inner ]]; then
    program="$2"; file="$3"; train="$4"; logs="$5"; until="$6"; presses="$7"; samples="$8"
    openbox > "$logs/openbox.out" 2>&1 &
    cd "$program"
    ./RunActivity -start -timetable "$file" "$train" 0 0 0 > "$logs/run.out" 2>&1 &
    pid=$!
    loaded=0
    for _ in $(seq 1 900); do
        sleep 2
        kill -0 "$pid" 2>/dev/null || { echo "Open Rails exited while loading"; exit 1; }
        if grep -q "State(GameStateViewer3D)" "$logs/OpenRailsLog.txt" 2>/dev/null; then loaded=1; break; fi
    done
    [[ "$loaded" == 1 ]] || { echo "Open Rails did not finish loading"; kill "$pid"; exit 1; }
    if [[ "$until" == load ]]; then
        sleep 5
        kill "$pid" 2>/dev/null || true
        exit 0
    fi
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
    for _ in $(seq 1 6); do
        before="$(now)"; sleep 3
        [[ "$(now)" != "$before" ]] && break
        focus; press Escape
    done
    focus; press Alt_L a; sleep 2
    for _ in $(seq 1 "$presses"); do press Control_L Alt_L Page_Up; done
    : > "$samples"
    still=0
    last=""
    while kill -0 "$pid" 2>/dev/null; do
        time="$(now)"
        if [[ "$time" == "$last" ]]; then
            (( ++still >= 30 )) && { echo "the game clock stopped at $time"; break; }
        else
            still=0
        fi
        last="$time"
        curl -s -m 10 http://localhost:2150/API/HUD/7 | python3 -c '
import json, sys
try:
    table = json.load(sys.stdin)["extraTable"]
except Exception:
    sys.exit(0)
print(json.dumps({"time": float(sys.argv[1]), "wall": float(sys.argv[2]), "hud": {"extraTable": table}}))' \
            "$time" "$(date +%s.%N)" >> "$samples" || true
        python3 -c "import sys; sys.exit(0 if float('$time') >= $until else 1)" && break
        sleep "${ORTS_SAMPLE:-1}"
    done
    kill "$pid" 2>/dev/null || true
    exit 0
fi

if [[ $# -lt 6 ]]; then
    echo "usage: $0 <game-root> <route> <work-dir> <timetable file> <player train> <until hh:mm|load> [speed presses]" >&2
    exit 2
fi
game_root="$(cd "$1" && pwd)"
route="$2"
work="$(mkdir -p "$3" && cd "$3" && pwd)"
timetable="$4"
player="$5"
if [[ "$6" == load ]]; then
    until=load
else
    until="$(python3 -c "import sys; h, m = sys.argv[1].split(':'); print(int(h) * 3600 + int(m) * 60)" "$6")"
fi
presses="${7:-7}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
program="${ORTS_PROGRAM:-$repo/../openrails-linux-spike/Program}"
if [[ -z "${DOTNET_ROOT:-}" && -d "$HOME/.local/dotnet" ]]; then
    export DOTNET_ROOT="$HOME/.local/dotnet"
fi
[[ -x "$program/RunActivity" ]] || { echo "no RunActivity in $program (set ORTS_PROGRAM)" >&2; exit 2; }

# The view: the game root's entries linked, the route a folder of links with
# its own ACTIVITIES/OPENRAILS holding links to the route's timetables and
# to the one run.
view="$work/MSTS"
if [[ ! -e "$view" ]]; then
    mkdir -p "$view/ROUTES/$route/ACTIVITIES/OPENRAILS"
    for entry in "$game_root"/*; do
        name="$(basename "$entry")"
        [[ "$name" == ROUTES ]] || ln -s "$entry" "$view/$name"
    done
    for entry in "$game_root/ROUTES/$route"/*; do
        name="$(basename "$entry")"
        [[ "$name" == ACTIVITIES || "$name" == PATHS ]] || ln -s "$entry" "$view/ROUTES/$route/$name"
    done
    # Open Rails' timetable mode lower-cases a train's path name before it
    # opens the file (ProcessTimetable.cs, ExtractPathString): on Linux the
    # route's PATHS is a folder of links here, each path also under its
    # lower-case name.
    mkdir -p "$view/ROUTES/$route/PATHS"
    for entry in "$game_root/ROUTES/$route/PATHS"/*; do
        name="$(basename "$entry")"
        ln -s "$entry" "$view/ROUTES/$route/PATHS/$name"
        lower="${name,,}"
        [[ -e "$view/ROUTES/$route/PATHS/$lower" ]] || ln -s "$entry" "$view/ROUTES/$route/PATHS/$lower"
    done
    for entry in "$game_root/ROUTES/$route/ACTIVITIES/OPENRAILS"/*; do
        ln -s "$entry" "$view/ROUTES/$route/ACTIVITIES/OPENRAILS/$(basename "$entry")"
    done
    touch "$view/TSRE_SCRATCH"
fi
[[ -e "$view/TSRE_SCRATCH" ]] || { echo "$view was not made by this script" >&2; exit 2; }
folder="$view/ROUTES/$route/ACTIVITIES/OPENRAILS"
if [[ -f "$timetable" ]]; then
    linked="$folder/$(basename "$timetable")"
    [[ -e "$linked" ]] || ln -s "$(cd "$(dirname "$timetable")" && pwd)/$(basename "$timetable")" "$linked"
    file="$linked"
else
    file="$folder/$timetable"
fi
[[ -e "$file" ]] || { echo "no timetable $timetable" >&2; exit 2; }
# Open Rails names the player's train "<description>:<name>"; the
# description is where the first #comment row and column meet.
description="$(python3 - "$file" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
for bom, codec in ((b'\xff\xfe', 'utf-16-le'), (b'\xfe\xff', 'utf-16-be'), (b'\xef\xbb\xbf', 'utf-8')):
    if data.startswith(bom):
        text = data[len(bom):].decode(codec)
        break
else:
    text = data.decode('utf-8')
rows = [line.split(text[0]) for line in text.replace('\r\n', '\n').split('\n') if line]
column = next((c for c, cell in enumerate(rows[0]) if cell.strip().lower() == '#comment'), -1)
for row in rows[1:]:
    if row and row[0].strip().lower().startswith('#comment') and 0 <= column < len(row):
        print(row[column].strip())
        break
PY
)"

logs="$work/logs"
mkdir -p "$logs" "$work/config/Open Rails"
rm -f "$logs/OpenRailsLog.txt"
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
samples="$logs/$(echo "$player" | tr -c 'A-Za-z0-9_-' '_').jsonl"
# Its own process group: the subshell becomes setsid, then xvfb-run.
(XDG_CONFIG_HOME="$work/config" exec setsid xvfb-run -a -s "-screen 0 800x600x24" \
    "$repo/scripts/orts-timetable-run.sh" --inner "$program" "$file" "$description:$player" "$logs" "$until" \
    "$presses" "$samples" > "$logs/run-outer.out" 2>&1) &
runner=$!
wait "$runner" || true
kill -- "-$runner" 2>/dev/null || true
echo "Open Rails' log: $logs/OpenRailsLog.txt"
[[ -f "$samples" ]] && echo "$(wc -l < "$samples") samples in $samples"
true
