cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(TsreOsmTests LANGUAGES CXX)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Core)
endif()
set(TSRE_OSM_SOURCES ../../src/tsre/geo/osm/OsmPbf.cpp ../../src/tsre/geo/osm/OsmConverter.cpp ../../src/tsre/geo/osm/OsmDirectory.cpp ../../src/tsre/geo/osm/SortedPbfStore.cpp ../../src/tsre/geo/osm/OsmMultipolygon.cpp ../../src/tsre/geo/osm/OsmClasses.cpp ../../src/tsre/geo/osm/OsmOverview.cpp ../../src/tsre/geo/osm/OsmMapGeometry.cpp ../../src/tsre/geo/osm/OsmGeneralize.cpp)
add_executable(tsre_osm_tests OsmPbfTests.cpp OsmConverterTests.cpp OsmDirectoryTests.cpp OsmMultipolygonTests.cpp OsmClassesTests.cpp OsmOverviewTests.cpp OsmMapGeometryTests.cpp OsmGeneralizeTests.cpp legacy/OSMFeatures.cpp ${TSRE_OSM_SOURCES})
target_compile_features(tsre_osm_tests PRIVATE cxx_std_17)
target_include_directories(tsre_osm_tests PRIVATE ../../src)
target_link_libraries(tsre_osm_tests PRIVATE Qt6::Core)
get_filename_component(TSRE_OSM_ROOT "../.." ABSOLUTE)
target_compile_definitions(tsre_osm_tests PRIVATE TSRE_OSM_CLASSES_JSON="${TSRE_OSM_ROOT}/src/tsre/geo/osm/osm-map-classes.json")
qt_add_resources(tsre_osm_tests osm_classes PREFIX "/osm"
    BASE "${TSRE_OSM_ROOT}/src/tsre/geo/osm" FILES "${TSRE_OSM_ROOT}/src/tsre/geo/osm/osm-map-classes.json")
# legacy/OSMFeatures.* is the former map class table, kept as the classification parity reference.
# Offline checks only. Local data, opt-in:
#   tsre_osm_tests --count <file.osm.pbf> [nodes ways relations tags]
#   tsre_osm_tests --convert <source.osm.pbf> <output.tsre.osm.pbf> [threads] [zlib level] [write group MiB]
#   tsre_osm_tests --verify <source.osm.pbf> <output.tsre.osm.pbf>
#   tsre_osm_tests --scan <directory>
#   tsre_osm_tests --query <minLon> <minLat> <maxLon> <maxLat> <converted files...>
#   tsre_osm_tests --multipolygons <minLon> <minLat> <maxLon> <maxLat> <converted files...>
#   tsre_osm_tests --classes <converted files...>
#   tsre_osm_tests --overview <converted files...>
#   tsre_osm_tests --map-geometry <minLon> <minLat> <maxLon> <maxLat> <metresPerPixel> <converted files...>
add_test(NAME osm COMMAND tsre_osm_tests)
