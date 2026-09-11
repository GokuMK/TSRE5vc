qt_add_executable(tsre_token_tests
    TokenTests.cpp
    "${PROJECT_SOURCE_DIR}/src/tsre/tests/TokenIdTestSuite.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/TS.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/FileBuffer.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/ReadFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/world/TFileBakeMetadata.cpp"
)
target_include_directories(tsre_token_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tsre_token_tests PRIVATE Qt6::Core)
add_test(NAME simis_tokens COMMAND tsre_token_tests)
