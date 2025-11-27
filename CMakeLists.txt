cmake_minimum_required(VERSION 3.16)

project(TSRE5vc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Network Core Widgets OpenGL OpenGLWidgets WebSockets )

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    find_package(OpenGL REQUIRED)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(OpenAL REQUIRED)       
endif()

qt_standard_project_setup()

include_directories("./src")

file(GLOB TSRE5vc_SRC CONFIGURE_DEPENDS
    "./src/mzip/miniz"
    "./src/mzip/miniz/miniz.h"
    "./src/*.h"
    "./src/*.cpp"
    "./src/tsre/camera/*.h"
    "./src/tsre/camera/*.cpp"
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
    "./src/tsre/renderer/*.h"
    "./src/tsre/renderer/*.cpp"
    "./src/tsre/procedural/*.h"
    "./src/tsre/procedural/*.cpp"
    "./src/tsre/geo/*.h" 
    "./src/tsre/geo/*.cpp"
    "./src/tsre/coords/*.h"
    "./src/tsre/coords/*.cpp"
    "./src/tsre/world/objects/*.h"
    "./src/tsre/world/objects/*.cpp"    
    "./src/tsre/world/*.h"
    "./src/tsre/world/*.cpp"
    "./src/tsre/*.h"
    "./src/tsre/*.cpp"
    "./src/routeEditor/*.h"
    "./src/routeEditor/*.cpp"
    "./src/routeEditor/activity/*.h"
    "./src/routeEditor/activity/*.cpp"
    "./src/routeEditor/properties/*.h"
    "./src/routeEditor/properties/*.cpp"
    "./src/conEditor/*.h"
    "./src/conEditor/*.cpp"
    "./src/shapeViewer/*.h"
    "./src/shapeViewer/*.cpp"
)

qt_add_executable(TSRE5vc ${TSRE5vc_SRC})

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_include_directories(TSRE5vc PUBLIC C:/programy/openal-soft-1.18.2-bin/include)
    target_link_libraries(TSRE5vc PUBLIC C:/programy/openal-soft-1.18.2-bin/libs/Win64/libOpenAL32.dll.a)
    target_link_libraries(TSRE5vc PRIVATE OpenGL::GL )
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(TSRE5vc PUBLIC OpenAL::OpenAL )
endif()

target_link_libraries(TSRE5vc PRIVATE Qt6::Network Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::WebSockets )

install(TARGETS TSRE5vc
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    #RUNTIME DESTINATION "."
)
