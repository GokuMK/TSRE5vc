#!/usr/bin/env bash
# Capture renderer views of one route and compare them with an earlier capture.
# Usage: scripts/renderer-parity.sh <game-root> <route> <label> [baseline] [cases.json] [extra TSRE args...]
#
# Captures the cases file views under <output>/<route>/<label>/. When a
# baseline label is given, compares the two captures and writes report.json
# and report.md under <output>/<route>/. Typical use: capture "baseline"
# before a renderer change, then capture "current" after it and compare.
set -euo pipefail

if [[ $# -lt 3 ]]; then
    echo "usage: $0 <game-root> <route> <label> [baseline] [cases.json] [extra args...]" >&2
    exit 2
fi

repo="$(cd "$(dirname "$0")/.." && pwd)"
game_root="$1"
route="$2"
label="$3"
baseline="${4:-}"
cases="${5:-$repo/tests/renderer/parity-views.json}"
shift $(( $# >= 5 ? 5 : $# ))
binary="${TSRE_BINARY:-$repo/build/TSRE5vc}"

cd "$repo"
common=(--game-root "$game_root" --route "$route" --test-cases "$cases")

# Headless machines have no display; Xvfb with Mesa gives a software GL context.
with_display=()
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    with_display=(xvfb-run -a -s "-screen 0 1920x1080x24")
fi

"${with_display[@]}" "$binary" "${common[@]}" --test --test-suite=renderer-capture \
    --test-label "$label" "$@"

if [[ -n "$baseline" ]]; then
    # Comparing needs no GL context.
    QT_QPA_PLATFORM=offscreen "$binary" "${common[@]}" --test --test-suite=renderer-compare \
        --test-baseline "$baseline" --test-label "$label" "$@"
fi
