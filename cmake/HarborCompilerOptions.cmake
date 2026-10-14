# Compiler and linker settings shared by every Harbor C++ target. Link against
# harbor_compiler_options instead of setting flags per target.

add_library(harbor_compiler_options INTERFACE)

target_compile_features(harbor_compiler_options INTERFACE cxx_std_20)

target_compile_definitions(harbor_compiler_options INTERFACE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    # Windows 11 22H2 is the minimum supported OS.
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    NTDDI_VERSION=0x0A00000C
)

target_compile_options(harbor_compiler_options INTERFACE
    /W4
    /WX
    /permissive-
    /utf-8
    /Zc:__cplusplus
    /Zc:preprocessor
    /EHsc
    /sdl
    /guard:cf
    # Third-party headers are included with angle brackets; don't hold them to our warning level.
    /external:anglebrackets
    /external:W0
)

target_link_options(harbor_compiler_options INTERFACE
    /guard:cf
    # CET shadow stacks exist on x64 only.
    $<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},x64>:/CETCOMPAT>
)
