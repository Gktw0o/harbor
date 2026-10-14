find_package(nlohmann_json CONFIG REQUIRED)
find_package(wil CONFIG REQUIRED)

# Pure domain logic: no Win32 headers, so it is testable on any machine.
add_library(harbor_dock_core STATIC
    core/appbar/appbar_state.cpp
    core/ipc/codec.cpp
    core/island/appearance.cpp
    core/island/icon_file.cpp
    core/island/layout.cpp
    core/ipc/line_framer.cpp
    core/ipc/pipe_name.cpp
    core/json/strict_json.cpp
    core/logging/log_line.cpp
    core/taskbar/guard_policy.cpp
    core/taskbar/restore_record.cpp
)
target_include_directories(harbor_dock_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(harbor_dock_core
    PUBLIC harbor_compiler_options
    PRIVATE nlohmann_json::nlohmann_json
)

# Win32 implementations of the ports.
add_library(harbor_dock_adapters STATIC
    adapters/app_launcher.cpp
    adapters/composition_renderer.cpp
    adapters/data_dir.cpp
    adapters/file_log.cpp
    adapters/icon_files.cpp
    adapters/named_pipe_client.cpp
    adapters/process_job.cpp
    adapters/system_appearance.cpp
    adapters/win32_taskbar.cpp
)
# Composition, Direct2D and the shell come with the Windows SDK and the OS (C++/WinRT headers included):
# no packages.
target_link_libraries(harbor_dock_adapters
    PUBLIC harbor_dock_core WIL::WIL
    PRIVATE windowsapp d3d11 d2d1 dxgi dwmapi CoreMessaging ole32 oleaut32 shell32
)

# Application layer (windows, threads, wiring). A library so the tests can link it; main.cpp stays out.
add_library(harbor_dock_app STATIC
    app/command_line.cpp
    app/crash_guard.cpp
    app/dock_window.cpp
    app/explorer_worker.cpp
    app/launcher.cpp
    app/taskbar_guard.cpp
)
target_link_libraries(harbor_dock_app PUBLIC harbor_dock_adapters PRIVATE Shcore)

add_executable(harbor-dock WIN32
    app/main.cpp
    app/harbor-dock.manifest
)
target_link_libraries(harbor-dock PRIVATE harbor_dock_app)
