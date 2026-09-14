qt_add_executable(tsre_content_case_tests
    ContentCaseTests.cpp
    "${PROJECT_SOURCE_DIR}/src/contentCase/ContentCase.cpp"
    "${PROJECT_SOURCE_DIR}/src/contentCase/ContentCaseDocument.cpp"
    "${PROJECT_SOURCE_DIR}/src/contentCase/ContentCaseCommand.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/SimisTextReader.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/FileBuffer.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/ReadFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/TS.cpp"
)
target_include_directories(tsre_content_case_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tsre_content_case_tests PRIVATE Qt6::Core)
add_test(NAME content_case COMMAND tsre_content_case_tests)
