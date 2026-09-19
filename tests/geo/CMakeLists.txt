cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(TsreElevationTests LANGUAGES CXX)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
endif()
add_executable(tsre_elevation_tests ElevationTests.cpp ElevationDownloadTests.cpp CzechWcsTests.cpp ArcGisImageServerTests.cpp NoDataFillTests.cpp
    ../../src/tsre/geo/ElevationRaster.cpp ../../src/tsre/geo/ElevationSource.cpp
    ../../src/tsre/geo/ElevationDownload.cpp)
target_compile_features(tsre_elevation_tests PRIVATE cxx_std_17)
target_include_directories(tsre_elevation_tests PRIVATE ../../src)
target_link_libraries(tsre_elevation_tests PRIVATE Qt6::Core Qt6::Network)
get_filename_component(TSRE_GEO_ROOT "../.." ABSOLUTE)
target_compile_definitions(tsre_elevation_tests PRIVATE
    TSRE_GEO_FIXTURES="${TSRE_GEO_ROOT}/docs/tasks/geo/evidence/2026-09-16")
qt_add_resources(tsre_elevation_tests elevation_test_catalog PREFIX "/geo"
    BASE "${TSRE_GEO_ROOT}/src/tsre/geo"
    FILES "${TSRE_GEO_ROOT}/src/tsre/geo/elevation-datasets.json")
add_test(NAME elevation COMMAND tsre_elevation_tests)

# Explicit live-service benchmark; never part of ctest or a normal build.
add_executable(tsre_elevation_benchmark EXCLUDE_FROM_ALL ElevationBenchmark.cpp ElevationConcurrencyBenchmark.cpp
    ../../src/tsre/geo/ElevationRaster.cpp ../../src/tsre/geo/ElevationSource.cpp
    ../../src/tsre/geo/ElevationDownload.cpp)
target_compile_features(tsre_elevation_benchmark PRIVATE cxx_std_17)
target_include_directories(tsre_elevation_benchmark PRIVATE ../../src)
target_link_libraries(tsre_elevation_benchmark PRIVATE Qt6::Core Qt6::Network)
qt_add_resources(tsre_elevation_benchmark elevation_benchmark_catalog PREFIX "/geo"
    BASE "${TSRE_GEO_ROOT}/src/tsre/geo"
    FILES "${TSRE_GEO_ROOT}/src/tsre/geo/elevation-datasets.json")
