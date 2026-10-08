cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(TsreOsmTests LANGUAGES CXX)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Core)
endif()
set(TSRE_OSM_SOURCES ../../src/tsre/geo/osm/OsmPbf.cpp ../../src/tsre/geo/osm/OsmConverter.cpp ../../src/tsre/geo/osm/OsmDirectory.cpp ../../src/tsre/geo/osm/SortedPbfStore.cpp)
add_executable(tsre_osm_tests OsmPbfTests.cpp OsmConverterTests.cpp OsmDirectoryTests.cpp ${TSRE_OSM_SOURCES})
target_compile_features(tsre_osm_tests PRIVATE cxx_std_17)
target_include_directories(tsre_osm_tests PRIVATE ../../src)
target_link_libraries(tsre_osm_tests PRIVATE Qt6::Core)
# Offline checks only. Local data, opt-in:
#   tsre_osm_tests --count <file.osm.pbf> [nodes ways relations tags]
#   tsre_osm_tests --convert <source.osm.pbf> <output.tsre.osm.pbf> [threads] [zlib level]
#   tsre_osm_tests --verify <source.osm.pbf> <output.tsre.osm.pbf>
#   tsre_osm_tests --scan <directory>
#   tsre_osm_tests --query <minLon> <minLat> <maxLon> <maxLat> <converted files...>
add_test(NAME osm COMMAND tsre_osm_tests)
