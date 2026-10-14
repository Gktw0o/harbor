# Pure domain logic: no Win32 headers, so it is testable on any machine.
add_library(harbor_dock_core STATIC
    core/ipc/pipe_name.cpp
)
target_include_directories(harbor_dock_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(harbor_dock_core PUBLIC harbor_compiler_options)

add_executable(harbor-dock WIN32
    app/main.cpp
    app/dock_window.cpp
    app/harbor-dock.manifest
)
target_link_libraries(harbor-dock PRIVATE harbor_dock_core)
