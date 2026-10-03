#!/usr/bin/env bash
# Compare the legacy and gather renderers on one route.
# Usage: scripts/renderer-parity.sh <game-root> <route> [cases.json] [extra TSRE args...]
#
# Each pipeline is captured in its own process, started with that pipeline;
# runtime pipeline switching is never used. A third step compares the captures
# and writes report.json and report.md under the cases file "output" directory.
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "usage: $0 <game-root> <route> [cases.json] [extra args...]" >&2
    exit 2
fi

repo="$(cd "$(dirname "$0")/.." && pwd)"
game_root="$1"
route="$2"
cases="${3:-$repo/tests/renderer/parity-views.json}"
shift $(( $# >= 3 ? 3 : 2 ))
binary="${TSRE_BINARY:-$repo/build/TSRE5vc}"

cd "$repo"
common=(--game-root "$game_root" --route "$route" --test-cases "$cases")

# Headless machines have no display; Xvfb with Mesa gives a software GL context.
with_display=()
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    with_display=(xvfb-run -a -s "-screen 0 1920x1080x24")
fi

for pipeline in legacy gather; do
    "${with_display[@]}" "$binary" "${common[@]}" --test --test-suite=renderer-capture \
        --set "core.rendering.pipeline=$pipeline" "$@"
done

# Comparing needs no GL context.
QT_QPA_PLATFORM=offscreen "$binary" "${common[@]}" --test --test-suite=renderer-compare "$@"
