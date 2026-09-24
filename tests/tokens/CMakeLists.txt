qt_add_executable(tsre_token_tests
    TokenTests.cpp
    "${PROJECT_SOURCE_DIR}/src/tsre/tests/TokenIdTestSuite.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/TS.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/FileBuffer.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/ReadFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TerrainFileData.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFileExtensions.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFilePatchFlags.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFilePatchAccess.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFileBakeMetadata.cpp"
)
target_include_directories(tsre_token_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tsre_token_tests PRIVATE Qt6::Core)
add_test(NAME simis_tokens COMMAND tsre_token_tests)

# CPU-only codec benchmark: no textures, RAW data, GL or application startup.
qt_add_executable(tsre_terrain_file_benchmark EXCLUDE_FROM_ALL
    TerrainFileBenchmark.cpp
    TerrainFileLegacy.cpp
    TerrainFileLegacyBake.cpp
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TerrainFileData.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/TS.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/FileBuffer.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/ReadFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFileExtensions.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFilePatchFlags.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFilePatchAccess.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFileBakeMetadata.cpp"
)
target_include_directories(tsre_terrain_file_benchmark PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tsre_terrain_file_benchmark PRIVATE Qt6::Core)

qt_add_executable(tsre_terrain_file_tests
    TerrainFileTests.cpp
    "${PROJECT_SOURCE_DIR}/src/tsre/tests/TerrainFileTestSuite.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TerrainFileData.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFileExtensions.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFilePatchFlags.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFilePatchAccess.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFileBakeMetadata.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/TS.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/FileBuffer.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/ReadFile.cpp"
)
target_include_directories(tsre_terrain_file_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tsre_terrain_file_tests PRIVATE Qt6::Core)
add_test(NAME terrain_tfile COMMAND tsre_terrain_file_tests)

# Standalone recovery/parser checks; no terrain renderer, route startup or GL context.
qt_add_executable(tsre_quadtree_tests
    QuadTreeTests.cpp
    "${PROJECT_SOURCE_DIR}/src/tsre/tests/QuadTreeRecoveryTestSuite.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/QuadTree.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/QuadTreeRecovery.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TerrainInfo.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TerrainFileData.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFileExtensions.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFilePatchFlags.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFilePatchAccess.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFileBakeMetadata.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/TS.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/FileBuffer.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/ReadFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/ParserX.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/SimisTextReader.cpp"
)
target_compile_definitions(tsre_quadtree_tests PRIVATE TSRE_QUADTREE_CORE_ONLY)
target_include_directories(tsre_quadtree_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tsre_quadtree_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
add_test(NAME quadtree_recovery COMMAND tsre_quadtree_tests)
