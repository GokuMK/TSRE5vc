#!/bin/bash
# Builds the design-stage OSM benchmarks. Run from this directory; needs Qt6Gui, zlib, libdeflate.
# Fetches libosmium + protozero (header-only, BSL-1.0) into ./deps; nothing is added to the TSRE build.
set -e
cd "$(dirname "$0")"
REPO=$(git rev-parse --show-toplevel); SRC=$REPO/src
mkdir -p deps
[ -d deps/libosmium ] || git clone -q --depth 1 https://github.com/osmcode/libosmium.git deps/libosmium
[ -d deps/protozero ] || git clone -q --depth 1 https://github.com/mapbox/protozero.git deps/protozero
# Splice the current MapDataOSM::loadData()/draw() bodies so the baseline is today's code.
M=$SRC/tsre/geo/MapDataOSM.cpp
sed -n '78,115p' $M > draw_head.inc; sed -n '116,125p' $M > draw_points_current.inc
sed -n '126,480p' $M > draw_tail.inc; sed -n '622,753p' $M > xml_body.inc
INC="-I. -I$SRC -Ideps/libosmium/include -Ideps/protozero/include"
g++ -O2 -std=c++17 $INC count.cpp -o count -lz -lpthread
g++ -O2 -std=c++17 $INC blobstats.cpp -o blobstats -lz -lpthread
g++ -O2 -std=c++17 -I$SRC/mzip/miniz inflate.cpp -o inflate -lz -ldeflate -lpthread
g++ -O2 -std=c++17 -fPIC $INC $(pkg-config --cflags Qt6Gui) osmbench.cpp $SRC/tsre/geo/OSMFeatures.cpp \
    -o osmbench $(pkg-config --libs Qt6Gui) -lz -lpthread
g++ -O2 -std=c++17 -I$SRC -Ideps/libosmium/include -Ideps/protozero/include hybrid.cpp -o hybrid -lz -ldeflate -lzstd -lpthread
g++ -O2 -std=c++17 -Ideps/protozero/include convert.cpp -o convert -ldeflate -lpthread
g++ -O2 -std=c++17 -Ideps/libosmium/include -Ideps/protozero/include verify.cpp -o verify -lz -lpthread
g++ -O2 scanp.cpp -o scanp
g++ -O2 -std=c++17 -DUSE_MINIZ -Ideps/protozero/include -I$SRC/mzip/miniz convert.cpp -o convert_mz -ldeflate -lpthread
