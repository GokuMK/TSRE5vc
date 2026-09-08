find_package(OpenGL REQUIRED)
set(ACE_TEXTURE_ROOT "${PROJECT_SOURCE_DIR}/src/tsre/texture")
qt_add_executable(tsre_ace_tool
    AceTool.cpp AceTestSupport.cpp
    "${ACE_TEXTURE_ROOT}/AceDocument.cpp"
    "${ACE_TEXTURE_ROOT}/DxtCodec.cpp"
    "${ACE_TEXTURE_ROOT}/AceLib.cpp" "${ACE_TEXTURE_ROOT}/AceLib.h"
    "${ACE_TEXTURE_ROOT}/AceLibLegacy.cpp" "${ACE_TEXTURE_ROOT}/AceLibLegacy.h"
    "${ACE_TEXTURE_ROOT}/Texture.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/ReadFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsre/fileFunctions/FileBuffer.cpp"
)
target_include_directories(tsre_ace_tool PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tsre_ace_tool PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL OpenGL::GL)
add_test(NAME ace_codec COMMAND tsre_ace_tool --self-test)
set_tests_properties(ace_codec PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
