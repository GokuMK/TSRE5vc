#!/bin/bash
# All timing runs, sequential, on an idle machine.
cd "$(dirname "$0")"
POM=/home/arch/OSM/Poland/pomorskie-261006.osm.pbf; PL=/home/arch/OSM/poland-261006.osm.pbf
evict(){ python3 -c "import os,sys;[os.posix_fadvise(os.open(p,os.O_RDONLY),0,0,os.POSIX_FADV_DONTNEED) for p in sys.argv[1:]]" "$@"; }
TCZ="18.7807 54.0833 18.8121 54.1017"; GDA="18.6283 54.3465 18.6597 54.3649"; RUR="17.8843 54.2408 17.9157 54.2592"; WAW="20.9965 52.2205 21.0279 52.2389"
ROUTE="18.3 53.9 19.2 54.45"
echo "## load at start: $(cat /proc/loadavg)"
echo "## decode only"
for f in $POM $PL; do ./count $f; OSMIUM_POOL_THREADS=3 ./count $f; done
evict $PL; echo -n "cold: "; ./count $PL
for f in $POM $PL; do for m in z l m; do ./inflate $f $m 12; ./inflate $f $m 1; done; done
echo "## current path (XML + draw)"
# Same four OSM API bboxes MapDataOSM::load() requests for each tile (fetched once, kept locally).
mkdir -p api
fetch(){ [ -s "api/$1_$2.osm" ] || curl -s -A "TSRE5-osm-benchmark/0.1" -o "api/$1_$2.osm" "https://www.openstreetmap.org/api/0.6/map?bbox=$2"; }
for b in 18.7807,54.0833,18.7964,54.0925 18.7964,54.0925,18.8121,54.1017 18.7964,54.0833,18.8121,54.0925 18.7807,54.0925,18.7964,54.1017; do fetch tczew $b; done
for b in 18.6283,54.3465,18.6440,54.3557 18.6440,54.3557,18.6597,54.3649 18.6440,54.3465,18.6597,54.3557 18.6283,54.3557,18.6440,54.3649; do fetch gdansk $b; done
./osmbench xml $TCZ 4096 api/tczew_*.osm 2>&1 | tail -1
./osmbench xml $GDA 4096 api/gdansk_*.osm 2>&1 | tail -1
echo "## import stages pomorskie (no mp)"
for e in "NOLOC=1 STAGE=0" "NOLOC=1 STAGE=1" "STAGE=0" "STAGE=2" "STAGE=9"; do echo -n "$e: "; env $e ./osmbench import $POM /dev/null --nomp 2>&1 | tail -1 | grep -o 'nodes+ways pass [0-9.]*s'; done
echo "## parallel import (ways only, no write)"
for t in 1 4 12; do ./osmbench pimport $POM $t 2>&1 | tail -1; done
for t in 4 12; do ./osmbench pimport $PL $t 2>&1 | tail -1; done
echo "## import pomorskie"
./osmbench import $POM pom.bin 2>&1 | tail -1
./osmbench import $POM pom_tags.bin --tags 2>&1 | tail -1
echo "## route-area import (60x60 km) from regional vs national file"
./osmbench import $POM route_pom.bin $ROUTE 2>&1 | tail -1
./osmbench import $PL route_pl.bin $ROUTE 2>&1 | tail -1
echo "## import Poland"
./osmbench import $PL pl.bin 2>&1 | tail -1
echo "## tile queries (4096 px)"
for t in "$TCZ" "$GDA" "$RUR"; do evict pom.bin; echo -n "pom cold [$t] "; ./osmbench query pom.bin $t 4096 2>&1 | tail -1; done
evict pl.bin; echo -n "pl cold [Warsaw] "; ./osmbench query pl.bin $WAW 4096 2>&1 | tail -1
echo -n "pom [Gdansk] 2048px "; ./osmbench query pom.bin $GDA 2048 2>&1 | tail -1
echo "## batch render, route area, 2 km tiles"
./osmbench batch pom.bin $ROUTE 0.0184 4096 12 2>&1 | tail -1
./osmbench batch pom.bin $ROUTE 0.0184 4096 4 2>&1 | tail -1
./osmbench batch pom.bin $ROUTE 0.0184 2048 12 2>&1 | tail -1
ls -la *.bin | awk '{printf "%s %.0f MB\n",$9,$5/1e6}'
echo "## load at end: $(cat /proc/loadavg)"
echo "## hybrid variants (run after the grid caches above exist)"
for t in "$TCZ" "$GDA" "$RUR"; do for th in 12 4 1; do ./hybrid tile $POM pom.bin $t $th; done; done
./hybrid blobs $PL 12; for th in 12 4; do ./hybrid tile $PL pl.bin $WAW $th; done
for l in 0 256 1024; do ./hybrid route $POM pom.bin $ROUTE 0.0184 $l 12; done; ./hybrid route $POM pom.bin $ROUTE 0.0184 0 4
./hybrid compact pom.bin; ./hybrid compact pl.bin
./hybrid lowrite $POM pom_lo.osm.pbf
for t in "$TCZ" "$GDA" "$RUR"; do ./hybrid lotile pom_lo.osm.pbf $t 12; ./hybrid lotile pom_lo.osm.pbf $t 1; done
echo "## full conversion (model B); TMP must be on the SSD, not tmpfs"
TMP=${TMP_SSD:-/home/arch/OSM/convtmp}; mkdir -p $TMP
./convert convert $POM $TMP/pom_b.osm.pbf $TMP 12; ./verify $POM $TMP/pom_b.osm.pbf
for th in 12 4; do evict $PL; ./convert convert $PL $TMP/pl_b.osm.pbf $TMP $th; done
./verify $PL $TMP/pl_b.osm.pbf; ./count $TMP/pl_b.osm.pbf
evict $TMP/pl_b.osm.pbf; ./scanp $TMP/pl_b.osm.pbf; ./scanp $TMP/pl_b.osm.pbf
for th in 12 4 1; do ./convert tile $TMP/pl_b.osm.pbf $WAW $th; done
LEVEL=1 ./convert convert $PL $TMP/pl_b1.osm.pbf $TMP 12; rm -f $TMP/pl_b1.osm.pbf
echo "## converter on miniz (already vendored in src/mzip) instead of libdeflate"
./convert_mz convert $POM $TMP/pom_mz.osm.pbf $TMP 12; ./verify $POM $TMP/pom_mz.osm.pbf
for lv in 6 1; do LEVEL=$lv ./convert_mz convert $PL $TMP/pl_mz.osm.pbf $TMP 12; done; LEVEL=6 ./convert_mz convert $PL $TMP/pl_mz.osm.pbf $TMP 4
for th in 12 4 1; do ./convert_mz tile $TMP/pl_mz.osm.pbf $WAW $th; done
rm -rf $TMP
