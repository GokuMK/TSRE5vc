#!/usr/bin/env bash
# MaSzyna half of the TSRE sound comparison: builds eu07 with the sound log patch (once), prepares a run
# directory mirroring the MaSzyna content, and drives the ET22 of scenery/tsre_sound_compare.scn under Xvfb.
#
#   run.sh <output dir>
#
# Writes to <output dir>: sounds.csv, state.csv (the patch), marks.csv (drive.py), maszyna.wav (OpenAL
# Soft's wave backend), log.txt and errors.txt (eu07), drive.log, eu07.out.
#
# Environment (defaults in brackets):
#   WORK            work directory for the source copy, build and run directory [/tmp/eu07-sound-compare]
#   MASZYNA_SRC     MaSzyna source checkout, read only [~/NetBeansProjects/maszyna]
#   MASZYNA_CONTENT MaSzyna game data, read only [~/ORTS/maszyna]
#   DISPLAY_NUM     Xvfb display number [91]
#   JOBS            build jobs [8]
#   SEED            eu07's -seed, picks the .mmd random includes (horn, converter/compressor set) [tsre]
#   RENDER          none: the scene is not drawn (gfx.skiprendering, about 170 fps on llvmpipe), legacy: the
#                   fixed-function renderer (about 12 fps), full: the shader renderer (about 7 fps) [none]
#   REBUILD=1       rebuilds even when the binary is there (it is rebuilt anyway when the patch changes)
#   BUILD_ONLY=1    stops after the build
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${1:?usage: run.sh <output dir>}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

WORK="${WORK:-/tmp/eu07-sound-compare}"
MASZYNA_SRC="${MASZYNA_SRC:-$HOME/NetBeansProjects/maszyna}"
MASZYNA_CONTENT="${MASZYNA_CONTENT:-$HOME/ORTS/maszyna}"
DISPLAY_NUM="${DISPLAY_NUM:-91}"
JOBS="${JOBS:-8}"
SEED="${SEED:-tsre}"
RENDER="${RENDER:-none}"
COMMIT=66a940d4

src="$WORK/eu07-src"
build="$WORK/eu07-build"
run="$WORK/run"
eu07="$build/bin/eu07"
mkdir -p "$WORK"

log() { echo "[run.sh] $*" >&2; }

# --- build -------------------------------------------------------------------------------------------
# submodules the build needs and the commits' GitHub repositories (the commits come from the checkout)
submodules="ref/glfw glfw/glfw
ref/glm g-truc/glm
ref/json nlohmann/json
ref/asio chriskohlhoff/asio
betterRenderer/thirdparty/yaml-cpp jbeder/yaml-cpp"

build_eu07() {
    local head path repo sha excludes=()
    head="$(git -C "$MASZYNA_SRC" rev-parse --short=8 HEAD)"
    if [ "$head" != "$COMMIT" ]; then
        log "warning: $MASZYNA_SRC is at $head, the patch was made for $COMMIT"
    fi
    # the copy keeps the file times of the checkout, so ninja rebuilds only what the patch touches
    log "copying $MASZYNA_SRC to $src (without .git)"
    while read -r path repo; do excludes+=(--exclude "/$path/"); done <<< "$submodules"
    rsync -a --delete --exclude .git "${excludes[@]}" "$MASZYNA_SRC/" "$src/"
    # submodules at the commits the checkout pins (read with git ls-tree, nothing is written there)
    while read -r path repo; do
        if [ -n "$(ls -A "$src/$path" 2>/dev/null)" ]; then continue; fi
        sha="$(git -C "$MASZYNA_SRC" ls-tree HEAD "$path" | awk '{print $3}')"
        log "fetching $repo $sha into $path"
        mkdir -p "$WORK/deps" "$src/$path"
        curl -sSfL "https://github.com/$repo/archive/$sha.tar.gz" -o "$WORK/deps/${path//\//_}.tar.gz"
        tar xzf "$WORK/deps/${path//\//_}.tar.gz" -C "$src/$path" --strip-components=1
    done <<< "$submodules"
    log "applying sound-log.patch"
    patch -d "$src" -p1 --quiet < "$here/sound-log.patch"
    cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DWITH_PYTHON=OFF -DWITH_UART=OFF -DWITH_DISCORD_RPC=OFF -DWITH_TESTS=OFF -DWITH_OPENVR=OFF \
        -DWITH_CRASHPAD=OFF -DADD_VERSION_SUFFIX=OFF > "$WORK/cmake.log"
    log "building eu07 (log: $WORK/build.log)"
    ninja -C "$build" -j "$JOBS" eu07 > "$WORK/build.log"
    cp "$here/sound-log.patch" "$WORK/applied.patch"
}

if [ ! -x "$eu07" ] || [ "${REBUILD:-0}" = 1 ] || ! cmp -s "$here/sound-log.patch" "$WORK/applied.patch"; then
    build_eu07
fi
if [ "${BUILD_ONLY:-0}" = 1 ]; then
    log "built $eu07"
    exit 0
fi

# --- run directory -----------------------------------------------------------------------------------
# every top-level entry of the content is a symlink, except: eu07.ini and the input ini files (copies, the
# keymapper may write the keyboard one), scenery/ (a real directory of symlinks plus the test scenery,
# eu07 writes its scenery caches there) and screenshots/
log "preparing $run"
rm -rf "$run"
mkdir -p "$run/scenery" "$run/screenshots" "$run/home"
for entry in "$MASZYNA_CONTENT"/* "$MASZYNA_CONTENT"/.[!.]*; do
    [ -e "$entry" ] || continue
    name="$(basename "$entry")"
    case "$name" in
        eu07.ini|eu07_input-*.ini) cp "$entry" "$run/$name" ;;
        scenery|screenshots) ;;
        *) ln -s "$entry" "$run/$name" ;;
    esac
done
for entry in "$MASZYNA_CONTENT"/scenery/*; do
    ln -s "$entry" "$run/scenery/$(basename "$entry")"
done
cp "$here/scenery/tsre_sound_compare.scn" "$run/scenery/"
# settings for a software-rendered, windowed run; appended so they override the shipped values
cat >> "$run/eu07.ini" <<'EOF'

// --- TSRE sound compare (extra/sound-compare/maszyna/run.sh): small window, heavy effects off ---
sceneryfile tsre_sound_compare.scn
humanctrlvehicle ET22-1036
width 640
height 400
fullscreen no
fullscreenwindowed no
vsync no
captureonstart no
inactivepause no
pause no
multisampling 0
maxtexturesize 1024
shadows no
gfx.shadowmap.enabled no
gfx.envmap.enabled no
gfx.extraeffects no
gfx.postfx.motionblur.enabled no
gfx.postfx.chromaticaberration.enabled no
gfx.smoke no
splinefidelity 1
python.enabled no
convertmodels 0
multiplelogs no
EOF
case "$RENDER" in
    none) echo "gfx.skiprendering yes" >> "$run/eu07.ini" ;;
    legacy) echo "gfxrenderer legacy" >> "$run/eu07.ini" ;;
    full) ;;
    *) log "RENDER is none, legacy or full"; exit 1 ;;
esac
# OpenAL Soft writes what it mixes to a wave file instead of a sound card
cat > "$run/alsoft.conf" <<EOF
[general]
drivers=wave
[wave]
file=$out/maszyna.wav
EOF

# --- run ---------------------------------------------------------------------------------------------
display=":$DISPLAY_NUM"
rm -f "$out"/sounds.csv "$out"/state.csv "$out"/marks.csv "$out"/maszyna.wav "$out"/log.txt "$out"/errors.txt
pids=()
cleanup() {
    pkill -f "^$eu07 " 2>/dev/null || true
    for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
}
trap cleanup EXIT

if [ -e "/tmp/.X11-unix/X$DISPLAY_NUM" ]; then
    log "display $display is in use (or a stale /tmp/.X11-unix/X$DISPLAY_NUM); set DISPLAY_NUM"
    exit 1
fi
Xvfb "$display" -screen 0 800x600x24 -nolisten tcp > "$out/xvfb.log" 2>&1 &
pids+=($!)
for _ in $(seq 50); do [ -e "/tmp/.X11-unix/X$DISPLAY_NUM" ] && break; sleep 0.1; done
DISPLAY="$display" openbox > "$out/openbox.log" 2>&1 &
pids+=($!)
sleep 1
# no key auto-repeat: drive.py holds the line breaker key down
DISPLAY="$display" xset r off

# the content directory is mounted read only for eu07 when bubblewrap is there
sandbox=()
if command -v bwrap > /dev/null; then
    sandbox=(bwrap --die-with-parent --dev-bind / / --ro-bind "$MASZYNA_CONTENT" "$MASZYNA_CONTENT" --)
fi
log "starting eu07 (scenery tsre_sound_compare.scn, vehicle ET22-1036, seed $SEED, render $RENDER)"
(
    cd "$run"
    exec env DISPLAY="$display" HOME="$run/home" XDG_CONFIG_HOME="$run/home/.config" \
        ALSOFT_CONF="$run/alsoft.conf" ALSOFT_DRIVERS=wave EU07_SOUND_CSV="$out" \
        "${sandbox[@]}" "$eu07" -s tsre_sound_compare.scn -v ET22-1036 -seed "$SEED"
) > "$out/eu07.out" 2>&1 &
eu07pid=$!
pids+=($eu07pid)

python3 "$here/drive.py" --out "$out" --display "$display" --pid "$eu07pid" \
    --keys "$run/eu07_input-keyboard.ini" 2>&1 | tee "$out/drive.log"
status=${PIPESTATUS[0]}

# drive.py quits eu07; give it time to close the wave file
for _ in $(seq 100); do kill -0 "$eu07pid" 2>/dev/null || break; sleep 0.2; done
if kill -0 "$eu07pid" 2>/dev/null; then
    log "eu07 did not quit, killing it"
    pkill -f "^$eu07 " || true
    kill "$eu07pid" 2>/dev/null || true
fi
cp "$run/log.txt" "$run/errors.txt" "$out/" 2>/dev/null || true
log "done (drive.py exit $status): $out"
exit "$status"
