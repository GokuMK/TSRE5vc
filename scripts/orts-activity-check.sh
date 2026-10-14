#!/usr/bin/env bash
# Loads activities as TSRE saves them in Open Rails (task editor 07).
# Usage: scripts/orts-activity-check.sh <game-root> <route> <work-dir> [activity] [seconds]
#
# Makes <work-dir>/MSTS a view of the game root (symlinks; the route's
# ACTIVITIES, PATHS, SERVICES and TRAFFIC copied, so nothing is written into
# the game root), lets TSRE write variants of the activity into it (suite
# activity-export: resaved, Open Rails additions, own traffic pattern,
# waiting points), then runs each in Open Rails (RunActivity -start) for the
# given seconds after its 3D view appears. Open Rails gets its own settings
# under <work-dir>/config; its logs go to <work-dir>/logs. Fails when an
# activity does not reach the 3D view or its log has errors.
#
# ORTS_PROGRAM: the folder with RunActivity (default: the sibling
# openrails-linux-spike worktree's Program). DOTNET_ROOT: the .NET install
# (default ~/.local/dotnet when it exists).
set -euo pipefail

if [[ $# -lt 3 ]]; then
    echo "usage: $0 <game-root> <route> <work-dir> [activity] [seconds]" >&2
    exit 2
fi

repo="$(cd "$(dirname "$0")/.." && pwd)"
game_root="$(cd "$1" && pwd)"
route="$2"
work="$(mkdir -p "$3" && cd "$3" && pwd)"
activity="${4:-lclsrvce}"
seconds="${5:-150}"
binary="${TSRE_BINARY:-$repo/build/TSRE5vc}"
program="${ORTS_PROGRAM:-$repo/../openrails-linux-spike/Program}"
if [[ -z "${DOTNET_ROOT:-}" && -d "$HOME/.local/dotnet" ]]; then
    export DOTNET_ROOT="$HOME/.local/dotnet"
fi
if [[ ! -x "$program/RunActivity" ]]; then
    echo "no RunActivity in $program (set ORTS_PROGRAM)" >&2
    exit 2
fi

# The view of the game root, made again each time (only one this script made).
view="$work/MSTS"
if [[ -e "$view" && ! -e "$view/TSRE_SCRATCH" ]]; then
    echo "$view exists and was not made by this script" >&2
    exit 2
fi
rm -rf "$view"
mkdir -p "$view/ROUTES/$route"
for entry in "$game_root"/*; do
    [[ "$(basename "$entry")" == ROUTES ]] && continue
    ln -s "$entry" "$view/$(basename "$entry")"
done
for entry in "$game_root/ROUTES/$route"/*; do
    name="$(basename "$entry")"
    case "${name^^}" in
        ACTIVITIES|PATHS|SERVICES|TRAFFIC) cp -r "$entry" "$view/ROUTES/$route/$name" ;;
        *) ln -s "$entry" "$view/ROUTES/$route/$name" ;;
    esac
done
touch "$view/TSRE_SCRATCH"

# Open Rails' settings, apart from the user's.
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
EOF

TSRE_EXPORT_ACTIVITY="$activity" QT_QPA_PLATFORM=offscreen "$binary" --game-root "$view" --route "$route" \
    --test --test-suite activity-export

activities="$view/ROUTES/$route/ACTIVITIES"
stock="$(cd "$activities" && ls | grep -i -x "$activity.act" | head -1)"
failed=0
for name in "${stock%.*}" tsre_resaved tsre_additions tsre_own_traffic tsre_waits; do
    log="$logs/OpenRailsLog.txt"
    rm -f "$log"
    # Its own process group (the subshell becomes setsid, then xvfb-run).
    (cd "$program" && XDG_CONFIG_HOME="$work/config" exec setsid xvfb-run -a -s "-screen 0 1280x800x24" \
        ./RunActivity -start -activity "$activities/$name.act" > "$logs/$name.out" 2>&1) &
    runner=$!
    state=timeout
    for _ in $(seq 1 300); do
        sleep 2
        if ! kill -0 "$runner" 2>/dev/null; then state=exited; break; fi
        if [[ -f "$log" ]] && grep -q "State(GameStateViewer3D)" "$log"; then state=viewer; break; fi
    done
    [[ "$state" == viewer ]] && sleep "$seconds"
    # The whole process group: xvfb-run, Xvfb and RunActivity.
    kill -- "-$runner" 2>/dev/null || true
    wait "$runner" 2>/dev/null || true
    cp "$log" "$logs/$name.txt" 2>/dev/null || true
    errors="$(grep -c "^Error:" "$logs/$name.txt" 2>/dev/null || true)"
    echo "$name: $state, ${errors:-0} errors"
    if [[ "$state" != viewer || "${errors:-0}" != 0 ]]; then failed=1; fi
done

# Warnings the TSRE variants have and the stock activity has not.
strip() { grep "^Warning:" "$1" 2>/dev/null | sed 's/[0-9.]\+//g' | sort -u; }
for name in tsre_resaved tsre_additions tsre_own_traffic tsre_waits; do
    extra="$(comm -13 <(strip "$logs/${stock%.*}.txt") <(strip "$logs/$name.txt"))"
    [[ -n "$extra" ]] && { echo "$name: warnings the stock activity has not:"; echo "$extra"; }
done
exit "$failed"
