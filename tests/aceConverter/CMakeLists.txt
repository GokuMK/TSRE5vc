qt_add_executable(tsre_ace_converter_tests
    ConverterTests.cpp
    "${PROJECT_SOURCE_DIR}/src/aceConverter/AceConverter.cpp"
    "${PROJECT_SOURCE_DIR}/src/aceConverter/AceConverterCommand.cpp"
    "${PROJECT_SOURCE_DIR}/src/aceConverter/AceConverterWindow.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/texture/AceDocument.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/texture/DxtCodec.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/texture/DdsImage.cpp"
)
target_include_directories(tsre_ace_converter_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tsre_ace_converter_tests PRIVATE Qt6::Widgets Qt6::OpenGL)
add_test(NAME ace_converter COMMAND tsre_ace_converter_tests --app $<TARGET_FILE:TSRE5vc>)
set_tests_properties(ace_converter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
