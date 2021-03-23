cmake_minimum_required(VERSION 3.5)


## Project

project(xochip)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)


## NLohmann JSON

# Don't run tests
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(lib/nlohmann_json)


## MiniFB

set(MINIFB_BUILD_EXAMPLES OFF CACHE INTERNAL "")
add_subdirectory(lib/minifb)


## libAO

set(LIBAO_FIND_REQUIRED, TRUE)
find_package(LibAO)

if(NOT DEFINED LIBAO_INCLUDE_DIR OR
   NOT DEFINED LIBAO_LIBRARIES)
    message(
        FATAL_ERROR
            "Couldn't find libao"
    )
endif()


## Project targets

add_executable(xochip xochip.cpp)
target_link_libraries(xochip minifb ${LIBAO_LIBRARIES})
target_include_directories(xochip PRIVATE ${LIBAO_INCLUDE_DIR})
set_property(TARGET xochip PROPERTY CXX_STANDARD 17)

add_executable(launcher launcher.cpp)
target_link_libraries(launcher nlohmann_json::nlohmann_json)
set_property(TARGET launcher PROPERTY CXX_STANDARD 17)

