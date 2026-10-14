cmake_minimum_required(VERSION 3.25)
project(harbor VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "Harbor's C++ code builds with MSVC only. See README.md.")
endif()

# A preset for one architecture inside a developer shell for another would silently build the wrong
# binaries, so fail fast instead.
if(DEFINED HARBOR_TARGET_ARCH AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL HARBOR_TARGET_ARCH)
    message(FATAL_ERROR
        "This preset targets ${HARBOR_TARGET_ARCH}, but the compiler on PATH targets "
        "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}. Use scripts/build.ps1, or open a VS developer shell "
        "for ${HARBOR_TARGET_ARCH}, and delete build/ for this preset before reconfiguring.")
endif()

include(cmake/HarborCompilerOptions.cmake)

enable_testing()

add_subdirectory(src/dock)
add_subdirectory(tests/dock)
