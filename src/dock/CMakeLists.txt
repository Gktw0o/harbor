find_package(nlohmann_json CONFIG REQUIRED)
find_package(wil CONFIG REQUIRED)

# Pure domain logic: no Win32 headers, so it is testable on any machine.
add_library(harbor_dock_core STATIC
    core/ipc/codec.cpp
    core/ipc/line_framer.cpp
    core/ipc/pipe_name.cpp
)
target_include_directories(harbor_dock_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(harbor_dock_core
    PUBLIC harbor_compiler_options
    PRIVATE nlohmann_json::nlohmann_json
)

# Win32 implementations of the ports.
add_library(harbor_dock_adapters STATIC
    adapters/named_pipe_client.cpp
)
target_link_libraries(harbor_dock_adapters PUBLIC harbor_dock_core WIL::WIL)

add_executable(harbor-dock WIN32
    app/main.cpp
    app/dock_window.cpp
    app/harbor-dock.manifest
)
target_link_libraries(harbor-dock PRIVATE harbor_dock_adapters)
