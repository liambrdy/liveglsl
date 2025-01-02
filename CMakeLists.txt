cmake_minimum_required (VERSION 3.8)

project ("liveglsl")

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
	set (CMAKE_BUILD_TYPE "Debug" CACHE STRING "Set the build type")
endif ()

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(deps/raylib)

add_executable(liveglsl "liveglsl.cpp")

target_link_libraries(liveglsl PUBLIC raylib)

add_executable(fontpacker "fontpacker.cpp")
# target_compile_options(fontpacker PUBLIC "/linker /subsystem:windows /entry:mainCRTStartup")
# target_link_options(fontpacker PUBLIC "/linker /subsystem:windows /entry:mainCRTStartup")