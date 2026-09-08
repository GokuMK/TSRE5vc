# Compile the unchanged TSRE codec against a private, Qt-free subset.
# A consumer must never link this target and real Qt into the same binary.
function(tsre_add_ace_codec target)
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../.." ABSOLUTE)
    add_library(${target} STATIC
        "${root}/src/tsre/texture/AceDocument.cpp"
        "${root}/src/tsre/texture/DxtCodec.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/qt_compat/Compress.cpp")
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_include_directories(${target} PUBLIC
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/qt_compat" "${root}/src")
    target_compile_definitions(${target} PRIVATE MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO)
endfunction()
