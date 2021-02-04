cmake_minimum_required(VERSION 3.5)

## Project

project(xochip)


## NLohmann JSON

# Don't run tests
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(lib/nlohmann_json)

set(MINIFB_BUILD_EXAMPLES OFF CACHE INTERNAL "")
add_subdirectory(lib/minifb)

## Project targets

add_executable(xochip xochip.cpp)
target_link_libraries(xochip minifb)
set_property(TARGET xochip PROPERTY CXX_STANDARD 17)

add_executable(launcher launcher.cpp)
target_link_libraries(launcher nlohmann_json::nlohmann_json)
set_property(TARGET launcher PROPERTY CXX_STANDARD 17)

