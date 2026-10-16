cmake_minimum_required(VERSION 3.24)

project(Ouroboros
	VERSION 0.1.0
	DESCRIPTION "Ouroboros - a small production-grade 3D game engine (and its Snake game)"
	LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not supported. Use a preset, e.g. `cmake --preset windows`.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Statically link the C/C++ runtime on MSVC so exported games do not need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(OB_BUILD_EDITOR "Build the Ouroboros editor" ON)
option(OB_BUILD_TESTS "Build the Ouroboros unit and integration tests" ON)
option(OB_WARNINGS_AS_ERRORS "Treat compiler warnings in Ouroboros code as errors" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerSettings)
include(Dependencies)
include(Shaders)

add_subdirectory(Ouroboros)
add_subdirectory(OuroborosRuntime)

if(OB_BUILD_EDITOR)
	add_subdirectory(OuroborosEditor)
endif()

if(OB_BUILD_TESTS)
	enable_testing()
	add_subdirectory(OuroborosTests)
endif()
