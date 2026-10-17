# Geodrive for Godot: a GDExtension that runs the engine-free Core (Core/) inside Godot.
#
#   cmake -S Godot/extension -B <build dir> -DGODOT_CPP_DIR=<godot-cpp checkout>
#   cmake --build <build dir> --config Release
#
# godot-cpp: https://github.com/godotengine/godot-cpp (its CMake generates the bindings with Python 3). The library
# lands in Godot/bin, where Godot/geodrive.gdextension finds it.

cmake_minimum_required(VERSION 3.17)
project(geodrive_godot LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(GODOT_CPP_DIR "" CACHE PATH "A godot-cpp checkout")
if(NOT GODOT_CPP_DIR)
    message(FATAL_ERROR "Set GODOT_CPP_DIR to a godot-cpp checkout")
endif()
add_subdirectory(${GODOT_CPP_DIR} godot-cpp)

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../Core)
file(GLOB CORE_SOURCES
    ${CORE}/GeoTiles/*.cpp
    ${CORE}/GeoRoads/*.cpp
    ${CORE}/GeoGround/*.cpp
    ${CORE}/GeoMesh/*.cpp
    ${CORE}/GeoCar/*.cpp
    ${CORE}/GeoFFB/*.cpp
    ${CORE}/GeoWorld/*.cpp
    ${CORE}/GeoTraffic/*.cpp
    ${CORE}/GeoPeople/*.cpp
    ${CORE}/GeoFlight/*.cpp
    ${CORE}/GeoBody/*.cpp
    ${CORE}/GeoBuild/*.cpp
    ${CORE}/GeoHunt/*.cpp
    ${CORE}/GeoGame/*.cpp)

add_library(geodrive SHARED
    src/register_types.cpp
    src/geodrive_world.cpp
    src/geodrive_car.cpp
    src/geodrive_game.cpp
    src/geodrive_aircraft.cpp
    src/geodrive_structure.cpp
    src/geodrive_hunt.cpp
    src/geodrive_wheel.cpp
    src/geodrive_world_index.cpp
    src/geodrive_car_sound.cpp
    src/process_memory.cpp
    src/http_fetch.cpp
    src/landcover.cpp
    ${CORE_SOURCES}
    ${CORE}/third_party/zstd/zstddeclib.c)
target_include_directories(geodrive PRIVATE src ${CORE} ${CORE}/GeoTiles ${CORE}/third_party/zstd)
target_link_libraries(geodrive PRIVATE godot::cpp)
if(MSVC)
    target_compile_options(geodrive PRIVATE /utf-8 /EHsc)
    target_compile_definitions(geodrive PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# Godot/bin/geodrive.<platform>.<target>.<arch> (as the .gdextension file lists them)
set_target_properties(geodrive PROPERTIES
    OUTPUT_NAME "geodrive.windows.${GODOTCPP_TARGET}.x86_64"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/../bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/../bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
