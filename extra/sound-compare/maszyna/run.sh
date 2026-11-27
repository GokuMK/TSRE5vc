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
#   MASZYNA_SRC     a clone of MaSzyna's repository, read only [~/NetBeansProjects/maszyna]
#   MASZYNA_COMMIT  the MaSzyna commit built [57148761]. It is taken from the clone's git objects, so the
#                   branch the clone is on, a pull and local changes don't change the build. 66a940d4: the
#                   build of the reports of 2026-11-15/16
#   MASZYNA_CONTENT MaSzyna game data, read only [~/ORTS/maszyna]
#   DISPLAY_NUM     Xvfb display number [91]
#   JOBS            build jobs [8]
#   SEED            eu07's -seed, picks the .mmd random includes (horn, converter/compressor set) [tsre]
#   RENDER          none: the scene is not drawn (gfx.skiprendering, about 170 fps on llvmpipe), legacy: the
#                   fixed-function renderer (about 12 fps), full: the shader renderer (about 7 fps) [none]
#   REBUILD=1       rebuilds even when the binary is there (it is rebuilt anyway when the commit or a patch
#                   changes)
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
MASZYNA_COMMIT="${MASZYNA_COMMIT:-57148761}"

src="$WORK/eu07-src"
build="$WORK/eu07-build"
run="$WORK/run"
eu07="$build/bin/eu07"
mkdir -p "$WORK"

log() { echo "[run.sh] $*" >&2; }

if ! commit="$(git -C "$MASZYNA_SRC" rev-parse --verify --quiet "$MASZYNA_COMMIT^{commit}")"; then
    log "$MASZYNA_SRC has no commit $MASZYNA_COMMIT (git -C $MASZYNA_SRC fetch first?)"
    exit 1
fi
short="${commit:0:8}"

# --- build -------------------------------------------------------------------------------------------
# submodules the build needs and their GitHub repositories; the commits are the ones MASZYNA_COMMIT pins,
# and a submodule that commit doesn't have is skipped (ref/wamr came after 66a940d4)
submodules="ref/glfw glfw/glfw
ref/glm g-truc/glm
ref/json nlohmann/json
ref/asio chriskohlhoff/asio
ref/wamr bytecodealliance/wasm-micro-runtime
betterRenderer/thirdparty/yaml-cpp jbeder/yaml-cpp"

# applied over the commit: sound-log.patch, then each compat/ patch where it fits. A compat patch makes a commit
# build the way this script builds it (build-without-uart.patch: WITH_UART=OFF); one is skipped at a commit
# that hasn't its code yet or has the fix already
patches=("$here/sound-log.patch")
for patchfile in "$here"/compat/*.patch; do
    if [ -e "$patchfile" ]; then patches+=("$patchfile"); fi
done
# what a build is made from: the commit and the patches; a change of either builds again
stamp() { echo "$commit"; cat "${patches[@]}"; }

build_eu07() {
    local path repo sha tarball patchfile
    # another commit starts from an empty build directory: the exported files carry their commit's time,
    # older than the objects of the previous build, so ninja would not see them as changed
    if [ "$(head -n 1 "$WORK/applied.stamp" 2>/dev/null)" != "$commit" ]; then
        rm -rf "$build"
    fi
    # the source comes from the git objects, not the working tree: nothing is written into the clone
    log "exporting MaSzyna $short from $MASZYNA_SRC to $src"
    rm -rf "$src"
    mkdir -p "$src"
    git -C "$MASZYNA_SRC" archive "$commit" | tar -x -C "$src"
    while read -r path repo; do
        sha="$(git -C "$MASZYNA_SRC" ls-tree "$commit" "$path" | awk '{print $3}')"
        if [ -z "$sha" ]; then continue; fi
        mkdir -p "$src/$path"
        if [ -e "$MASZYNA_SRC/$path/.git" ] && git -C "$MASZYNA_SRC/$path" cat-file -e "$sha^{commit}" 2> /dev/null; then
            git -C "$MASZYNA_SRC/$path" archive "$sha" | tar -x -C "$src/$path"
        else
            tarball="$WORK/deps/${path//\//_}-$sha.tar.gz"
            if [ ! -s "$tarball" ]; then
                log "fetching $repo $sha into $path"
                mkdir -p "$WORK/deps"
                curl -sSfL "https://github.com/$repo/archive/$sha.tar.gz" -o "$tarball.part"
                mv "$tarball.part" "$tarball"
            fi
            tar xzf "$tarball" -C "$src/$path" --strip-components=1
        fi
    done <<< "$submodules"
    log "applying sound-log.patch"
    patch -d "$src" -p1 --quiet < "$here/sound-log.patch"
    for patchfile in "${patches[@]:1}"; do
        if patch -d "$src" -p1 --dry-run --forward --quiet < "$patchfile" > /dev/null 2>&1; then
            log "applying compat/$(basename "$patchfile")"
            patch -d "$src" -p1 --forward --quiet < "$patchfile"
        else
            log "compat/$(basename "$patchfile"): not for $short, skipped"
        fi
    done
    # the commit, for the header of sounds.csv
    printf '#define EU07_SOUND_LOG_COMMIT "%s"\n' "$short" > "$src/sound_log_commit.h"
    # the files the previous build's patches changed are built again: exported anew, they have the commit's
    # time back, older than their objects
    if [ -f "$WORK/applied.stamp" ]; then
        { grep -o '^+++ b/[^[:space:]]*' "$WORK/applied.stamp" || true; } | cut -c7- | while read -r path; do
            if [ -e "$src/$path" ]; then touch "$src/$path"; fi
        done
    fi
    if ! cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DWITH_PYTHON=OFF -DWITH_UART=OFF -DWITH_DISCORD_RPC=OFF -DWITH_TESTS=OFF -DWITH_OPENVR=OFF \
        -DWITH_CRASHPAD=OFF -DADD_VERSION_SUFFIX=OFF > "$WORK/cmake.log" 2>&1; then
        tail -n 20 "$WORK/cmake.log" >&2
        log "configuring MaSzyna $short failed, see $WORK/cmake.log"
        exit 1
    fi
    log "building eu07 (log: $WORK/build.log)"
    if ! ninja -C "$build" -j "$JOBS" eu07 > "$WORK/build.log"; then
        grep -m 20 -E 'error|FAILED' "$WORK/build.log" >&2 || true
        log "the build of MaSzyna $short failed, see $WORK/build.log"
        exit 1
    fi
    stamp > "$WORK/applied.stamp"
}

if [ ! -x "$eu07" ] || [ "${REBUILD:-0}" = 1 ] || ! cmp -s <(stamp) "$WORK/applied.stamp"; then
    build_eu07
fi
log "eu07 of MaSzyna $short"
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
mkdir -p "$run/scenery" "$run/screenshots" "$run/home" "$run/shaders"
for entry in "$MASZYNA_CONTENT"/* "$MASZYNA_CONTENT"/.[!.]*; do
    [ -e "$entry" ] || continue
    name="$(basename "$entry")"
    case "$name" in
        eu07.ini|eu07_input-*.ini) cp "$entry" "$run/$name" ;;
        scenery|screenshots|shaders) ;;
        *) ln -s "$entry" "$run/$name" ;;
    esac
done
for entry in "$MASZYNA_CONTENT"/scenery/*; do
    ln -s "$entry" "$run/scenery/$(basename "$entry")"
done
# the shaders of the commit built over the content's, as a MaSzyna build is unpacked over an install: the
# renderer of a newer commit needs its own
for entry in "$MASZYNA_CONTENT"/shaders/* "$src"/shaders/*; do
    [ -e "$entry" ] || continue
    ln -sfn "$entry" "$run/shaders/$(basename "$entry")"
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
# no key auto-repeat (-r): drive.py holds the line breaker key down. The server's own switch, so xset isn't needed
# (the machine rebuilt on 2026-11-25 has no xset; with it the old `xset r off` is run as well)
Xvfb "$display" -screen 0 800x600x24 -nolisten tcp -r > "$out/xvfb.log" 2>&1 &
pids+=($!)
for _ in $(seq 50); do [ -e "/tmp/.X11-unix/X$DISPLAY_NUM" ] && break; sleep 0.1; done
DISPLAY="$display" openbox > "$out/openbox.log" 2>&1 &
pids+=($!)
sleep 1
if command -v xset > /dev/null; then
    DISPLAY="$display" xset r off
fi

# the content directory is mounted read only for eu07 when bubblewrap is there
sandbox=()
if command -v bwrap > /dev/null; then
    sandbox=(bwrap --die-with-parent --dev-bind / / --ro-bind "$MASZYNA_CONTENT" "$MASZYNA_CONTENT" --)
fi
log "starting eu07 (scenery tsre_sound_compare.scn, vehicle ET22-1036, seed $SEED, render $RENDER)"
# on the Xvfb display: GLFW 3.4 takes Wayland when the session is a Wayland one (XDG_SESSION_TYPE=wayland and
# WAYLAND_DISPLAY set, as in a shell of a Wayland desktop), and eu07's window would open on that desktop instead
(
    cd "$run"
    exec env -u WAYLAND_DISPLAY XDG_SESSION_TYPE=x11 DISPLAY="$display" HOME="$run/home" XDG_CONFIG_HOME="$run/home/.config" \
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
