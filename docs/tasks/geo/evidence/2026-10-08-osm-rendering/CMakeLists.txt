# Triangulation benchmark of filled OSM areas (osm-rendering-design.md).
#   git clone --depth 1 https://github.com/mapbox/earcut.hpp.git earcut   (177bd66, 2026-09-28)
#   cmake -S . -B build -DTSRE_ROOT=<worktree> -DEARCUT=<earcut>/include && cmake --build build
#   build/earcut_bench <minLon> <minLat> <maxLon> <maxLat> <converted files...>
cmake_minimum_required(VERSION 3.22)
project(EarcutBench CXX)
find_package(Qt6 REQUIRED COMPONENTS Core)
set(O ${TSRE_ROOT}/src/tsre/geo/osm)
add_executable(earcut_bench earcut_bench.cpp ${O}/OsmPbf.cpp ${O}/SortedPbfStore.cpp ${O}/OsmMultipolygon.cpp
    ${O}/OsmClasses.cpp ${O}/OsmDirectory.cpp ${O}/OsmConverter.cpp ${O}/OsmOverview.cpp)
target_compile_features(earcut_bench PRIVATE cxx_std_17)
target_include_directories(earcut_bench PRIVATE ${TSRE_ROOT}/src ${EARCUT})
target_link_libraries(earcut_bench PRIVATE Qt6::Core)
qt_add_resources(earcut_bench osm_classes PREFIX "/osm" BASE "${O}" FILES "${O}/osm-map-classes.json")
