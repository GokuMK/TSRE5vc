cmake_minimum_required(VERSION 3.22)

project(TSRE5vc LANGUAGES CXX)

# Development builds show VERSION with -dev, following it when it changes
# (it is a configure dependency, and a cached -dev value of an older VERSION
# is replaced). A version given on the command line, as the release
# workflow does with the tag's, is kept.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TSRE5_BASE_VERSION LIMIT_COUNT 1)
if(DEFINED CACHE{TSRE5_VERSION} AND TSRE5_VERSION MATCHES "-dev$"
        AND NOT TSRE5_VERSION STREQUAL "${TSRE5_BASE_VERSION}-dev")
    unset(TSRE5_VERSION CACHE)
endif()
set(TSRE5_VERSION "${TSRE5_BASE_VERSION}-dev" CACHE STRING "Version shown by TSRE5vc")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TSRE5Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/TSRE5Version.h"
    @ONLY
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The QRhi renderer uses QRhi and QShaderBaker, which Qt publishes through its
# private Gui and ShaderTools modules; they tie the build to this Qt version.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 REQUIRED COMPONENTS Network Core Gui Widgets OpenGL OpenGLWidgets WebSockets LinguistTools
    GuiPrivate ShaderTools ShaderToolsPrivate)
# Release builds must have the QRhi renderer's Vulkan backend (its default):
# without Qt's Vulkan support or the Vulkan headers it is left out silently
# (RhiContext.h) and every user falls back to the OpenGL renderer. Checked
# here, when configuring, so the option changes nothing that is compiled.
option(TSRE5_REQUIRE_VULKAN "Stop when the QRhi Vulkan backend cannot be built" OFF)
if(TSRE5_REQUIRE_VULKAN)
    if(DEFINED QT_FEATURE_vulkan AND NOT QT_FEATURE_vulkan)
        message(FATAL_ERROR "TSRE5_REQUIRE_VULKAN: this Qt is built without Vulkan support")
    endif()
    if(NOT TARGET WrapVulkanHeaders::WrapVulkanHeaders)
        message(FATAL_ERROR "TSRE5_REQUIRE_VULKAN: Vulkan headers not found (set Vulkan_INCLUDE_DIR)")
    endif()
    message(STATUS "QRhi Vulkan backend: Qt Vulkan support and headers found")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    find_package(OpenGL REQUIRED)
endif()

# Sound plays through miniaudio (thirdparty/miniaudio), compiled into
# TSRE; it loads the system's audio libraries at run time.
find_package(Threads REQUIRED)

# Lua 5.5 (thirdparty/lua) for TSRE's scripts (src/tsre/script), compiled
# as C++: a Lua error unwinds as a C++ exception, so destructors in TSRE's
# code run. Its warnings are not ours to fix.
file(GLOB TSRE_LUA_SRC CONFIGURE_DEPENDS "./thirdparty/lua/*.c")
set_source_files_properties(${TSRE_LUA_SRC} PROPERTIES LANGUAGE CXX)
add_library(tsre_lua STATIC ${TSRE_LUA_SRC})
target_include_directories(tsre_lua SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lua")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # mkstemp rather than tmpnam in the os library (never opened whole).
    target_compile_definitions(tsre_lua PUBLIC LUA_USE_POSIX)
endif()
if(MSVC)
    target_compile_options(tsre_lua PRIVATE /w)
else()
    target_compile_options(tsre_lua PRIVATE -w)
endif()

# RmlUi 6.3 (thirdparty/rmlui) for the game UI (src/tsre/ui): its core, Lua
# plugin and debugger, Lua compiled as C++. Its own font engine (enabled by
# RMLUI_FONT_ENGINE_FREETYPE, after the library it normally uses) gets its
# glyphs from Qt: TSRE's RmlQtFontInterface.cpp is built in place of RmlUi's
# FreeType file, so there is no FreeType to find or ship.
set(TSRE_RMLUI "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rmlui")
file(GLOB_RECURSE TSRE_RMLUI_SRC CONFIGURE_DEPENDS
    "${TSRE_RMLUI}/Source/Core/*.cpp" "${TSRE_RMLUI}/Source/Lua/*.cpp" "${TSRE_RMLUI}/Source/Debugger/*.cpp")
list(FILTER TSRE_RMLUI_SRC EXCLUDE REGEX "/FontEngineDefault/FreeTypeInterface\\.cpp$")
add_library(tsre_rmlui STATIC ${TSRE_RMLUI_SRC} "./src/tsre/ui/RmlQtFontInterface.cpp")
target_include_directories(tsre_rmlui SYSTEM PUBLIC "${TSRE_RMLUI}/Include")
target_include_directories(tsre_rmlui PRIVATE "${TSRE_RMLUI}/Source/Core")
target_compile_definitions(tsre_rmlui PUBLIC RMLUI_STATIC_LIB RMLUI_LUA_AS_CXX
    PRIVATE RMLUI_FONT_ENGINE_FREETYPE "RMLUI_VERSION=\"6.3\"")
target_link_libraries(tsre_rmlui PUBLIC tsre_lua PRIVATE Qt6::Gui)
# Its warnings are not ours to fix.
if(MSVC)
    set_source_files_properties(${TSRE_RMLUI_SRC} PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(${TSRE_RMLUI_SRC} PROPERTIES COMPILE_OPTIONS "-w")
endif()

qt_standard_project_setup()

include_directories("./src")
# Third-party libraries (thirdparty/README.md), apart from TSRE's own code;
# a system include root, so their headers bring no warnings.
include_directories(SYSTEM "./thirdparty")

file(GLOB TSRE5vc_SRC CONFIGURE_DEPENDS
    "./thirdparty/miniz/miniz.h"
    "./thirdparty/earcut/earcut.hpp"
    "./src/*.h"
    "./src/*.cpp"
    "./src/settings/*.h"
    "./src/settings/*.cpp"
    "./src/settings/ui/*.h"
    "./src/settings/ui/*.cpp"
    "./src/tsre/camera/*.h"
    "./src/tsre/camera/*.cpp"
    "./src/tsre/map/*.h"
    "./src/tsre/map/*.cpp"
    "./src/tsre/ogl/*.h"
    "./src/tsre/ogl/*.cpp"
    "./src/tsre/math3d/*.h"
    "./src/tsre/math3d/*.cpp"
    "./src/tsre/fileFunctions/*.h"
    "./src/tsre/fileFunctions/*.cpp"
    "./src/tsre/gui/*.h"
    "./src/tsre/gui/*.cpp"
    "./src/tsre/hud/*.h"
    "./src/tsre/hud/*.cpp"
    "./src/tsre/shape/*.h"
    "./src/tsre/shape/*.cpp"
    "./src/tsre/sound/*.h"
    "./src/tsre/sound/*.cpp"
    "./src/tsre/texture/*.h"
    "./src/tsre/texture/*.cpp"
    "./src/tsre/tdb/*.h"
    "./src/tsre/tdb/*.cpp"
    "./src/tsre/trains/*.h"
    "./src/tsre/trains/*.cpp"
    "./src/tsre/sim/*.h"
    "./src/tsre/sim/*.cpp"
    "./src/tsre/units/*.h"
    "./src/tsre/units/*.cpp"
    "./src/tsre/script/*.h"
    "./src/tsre/script/*.cpp"
    "./src/tsre/ui/*.h"
    "./src/tsre/ui/*.cpp"
    "./src/tsre/renderer/*.h"
    "./src/tsre/renderer/*.cpp"
    "./src/tsre/renderer/rhi/*.h"
    "./src/tsre/renderer/rhi/*.cpp"
    "./src/tsre/procedural/*.h"
    "./src/tsre/procedural/*.cpp"
    "./src/tsre/geo/*.h" 
    "./src/tsre/geo/*.cpp"
    "./src/tsre/geo/osm/*.h"
    "./src/tsre/geo/osm/*.cpp"
    "./src/tsre/coords/*.h"
    "./src/tsre/coords/*.cpp"
    "./src/tsre/environment/*.h"
    "./src/tsre/environment/*.cpp"
    "./src/tsre/tests/*.h"
    "./src/tsre/tests/*.cpp"
    "./src/tsre/world/objects/*.h"
    "./src/tsre/world/objects/*.cpp"    
    "./src/tsre/world/*.h"
    "./src/tsre/world/*.cpp"
    "./src/tsre/*.h"
    "./src/tsre/*.cpp"
    "./src/routeEditor/*.h"
    "./src/routeEditor/*.cpp"
    "./src/routeEditor/tools/*.h"
    "./src/routeEditor/tools/*.cpp"
    "./src/routeEditor/activity/*.h"
    "./src/routeEditor/activity/*.cpp"
    "./src/routeEditor/properties/*.h"
    "./src/routeEditor/properties/*.cpp"
    "./src/conEditor/*.h"
    "./src/conEditor/*.cpp"
    "./src/shapeViewer/*.h"
    "./src/shapeViewer/*.cpp"
    "./src/shapeConverter/*.h"
    "./src/shapeConverter/*.cpp"
    "./src/aceConverter/*.h"
    "./src/aceConverter/*.cpp"
    "./src/contentCase/*.h"
    "./src/contentCase/*.cpp"
)

# Built into tsre_rmlui (above).
list(FILTER TSRE5vc_SRC EXCLUDE REGEX "/src/tsre/ui/RmlQtFontInterface\\.cpp$")
qt_add_executable(TSRE5vc ${TSRE5vc_SRC})
qt_add_resources(TSRE5vc elevation_catalog PREFIX "/geo"
    BASE "src/tsre/geo" FILES "src/tsre/geo/elevation-datasets.json")
qt_add_resources(TSRE5vc imagery_catalog PREFIX "/geo"
    BASE "src/tsre/geo" FILES "src/tsre/geo/imagery-datasets.json")
qt_add_resources(TSRE5vc osm_classes PREFIX "/osm"
    BASE "src/tsre/geo/osm" FILES "src/tsre/geo/osm/osm-map-classes.json")
# Shown by the About window: the ISC and MIT licences ask for their notices
# in every copy.
qt_add_resources(TSRE5vc earcut_licence PREFIX "/licences/earcut"
    BASE "thirdparty/earcut" FILES "thirdparty/earcut/LICENSE")
qt_add_resources(TSRE5vc lua_licence PREFIX "/licences/lua"
    BASE "thirdparty/lua" FILES "thirdparty/lua/LICENSE")
qt_add_resources(TSRE5vc rmlui_licence PREFIX "/licences/rmlui"
    BASE "thirdparty/rmlui" FILES "thirdparty/rmlui/LICENSE.txt"
    "thirdparty/rmlui/Include/RmlUi/Core/Containers/LICENSE.txt" "thirdparty/rmlui/Source/Debugger/LICENSE.txt")
target_include_directories(TSRE5vc PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(TSRE5vc PRIVATE OpenGL::GL )
endif()

target_link_libraries(TSRE5vc PRIVATE Qt6::Network Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::WebSockets
    Qt6::GuiPrivate Qt6::ShaderTools Qt6::ShaderToolsPrivate Threads::Threads ${CMAKE_DL_LIBS} tsre_lua tsre_rmlui)
# miniaudio is not ours to fix: no warnings from its implementation.
if(MSVC)
    set_source_files_properties(src/tsre/sound/Miniaudio.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(src/tsre/sound/Miniaudio.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

qt_add_translations(
    TSRE5vc
    SOURCE_TARGETS TSRE5vc
    TS_FILES
        translations/tsre_en.ts
        translations/tsre_pl.ts
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete
    LRELEASE_OPTIONS -nounfinished
    MERGE_QT_TRANSLATIONS
    QT_TRANSLATION_CATALOGS qtbase
)

install(TARGETS TSRE5vc
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    #RUNTIME DESTINATION "."
)

option(TSRE_BUILD_ACE_TESTS "Build standalone ACE codec, benchmark and GL probes" ON)
option(TSRE_BUILD_TOKEN_TESTS "Build standalone native SIMIS token tests" ON)
if(TSRE_BUILD_TOKEN_TESTS)
    enable_testing()
    add_subdirectory(tests/tokens)
endif()
if(TSRE_BUILD_ACE_TESTS)
    enable_testing()
    add_subdirectory(tests/ace)
    add_subdirectory(tests/aceConverter)
endif()

option(TSRE_BUILD_SHAPE_TESTS "Build standalone shape document tests" ON)
if(TSRE_BUILD_SHAPE_TESTS)
    enable_testing()
    add_subdirectory(tests/shapes)
endif()

option(TSRE_BUILD_CONTENT_CASE_TESTS "Build read-only content-case scanner tests" ON)
if(TSRE_BUILD_CONTENT_CASE_TESTS)
    enable_testing()
    add_subdirectory(tests/contentCase)
    add_subdirectory(tests/contentPaths)
endif()

option(TSRE_BUILD_GEO_TESTS "Build elevation raster, cache and sampling checks" ON)
if(TSRE_BUILD_GEO_TESTS)
    enable_testing()
    add_subdirectory(tests/geo)
endif()

option(TSRE_BUILD_OSM_TESTS "Build OSM PBF format and store checks" ON)
if(TSRE_BUILD_OSM_TESTS)
    enable_testing()
    add_subdirectory(tests/osm)
endif()

# The translation catalogues: IDs only, English complete, Polish consistent.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    enable_testing()
    add_test(NAME translations
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_translations.py)
endif()
