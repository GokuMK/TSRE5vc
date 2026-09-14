if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21)
    project(TSREContentCaseTests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    find_package(Qt6 REQUIRED COMPONENTS Core)
    enable_testing()
endif()

qt_add_executable(tsre_content_case_tests
    ContentCaseTests.cpp
    ContentCaseExecutionTests.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/contentCase/ContentCase.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/contentCase/ContentCaseDocument.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/contentCase/ContentCaseCommand.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/contentCase/ContentCaseExecution.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/tsre/fileFunctions/SimisTextReader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/tsre/fileFunctions/FileBuffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/tsre/fileFunctions/ReadFile.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/tsre/fileFunctions/TS.cpp"
)
target_include_directories(tsre_content_case_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(tsre_content_case_tests PRIVATE Qt6::Core)
add_test(NAME content_case COMMAND tsre_content_case_tests)
