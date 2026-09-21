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
