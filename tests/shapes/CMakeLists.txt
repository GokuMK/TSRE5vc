qt_add_executable(tsre_shape_document_tests
    ShapeDocumentTests.cpp
    "${PROJECT_SOURCE_DIR}/src/tsre/shape/SFileDocument.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/SimisTextReader.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/TS.cpp"
)
target_include_directories(tsre_shape_document_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(tsre_shape_document_tests PRIVATE SHAPE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(tsre_shape_document_tests PRIVATE Qt6::Core)
add_test(NAME shape_document COMMAND tsre_shape_document_tests)

# Manual stage benchmark: no GL context, rendering, textures or shape runtime.
qt_add_executable(tsre_shape_parser_bench
    ShapeParserBenchmark.cpp
    "${PROJECT_SOURCE_DIR}/src/tsre/shape/SFileDocument.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/SimisTextReader.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/ParserX.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/FileBuffer.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/ReadFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/TS.cpp"
)
target_include_directories(tsre_shape_parser_bench PRIVATE "${PROJECT_SOURCE_DIR}/src")
# ParserX includes legacy SFile's GL declarations; this executable never initializes GL.
target_link_libraries(tsre_shape_parser_bench PRIVATE Qt6::Core Qt6::OpenGLWidgets)
