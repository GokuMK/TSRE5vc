#!/usr/bin/env bash
# lamp-root.sh <msts-root> <route> <out-dir>: a game root of links to
# <msts-root> whose route also finds tsre_street_lamp.gltf in SHAPES, so
# lamp-views.json can place lamps without changing the route.
set -eu
src=$1; route=$2; out=$3
here=$(cd "$(dirname "$0")" && pwd)
rm -rf "$out"
mkdir -p "$out/ROUTES/$route/SHAPES"
for e in "$src"/*; do n=$(basename "$e"); [ "$n" = ROUTES ] || ln -s "$e" "$out/$n"; done
for e in "$src/ROUTES/$route"/*; do n=$(basename "$e"); [ "$n" = SHAPES ] || ln -s "$e" "$out/ROUTES/$route/$n"; done
for f in "$src/ROUTES/$route/SHAPES"/*; do ln -s "$f" "$out/ROUTES/$route/SHAPES/$(basename "$f")"; done
cp "$here/tsre_street_lamp.gltf" "$out/ROUTES/$route/SHAPES/"
echo "$out"
