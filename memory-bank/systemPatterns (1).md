# System Patterns

## Process architecture
```
┌──────────────────────┐   named pipe (JSON lines, v1)   ┌──────────────────────────┐
│ harbor-dock.exe      │◄───────────────────────────────►│ Harbor.Shell.exe (C#)    │
│ C++20, Win32,        │                                 │ WinUI 3: Settings,       │
│ DirectComposition +  │                                 │ onboarding, watchdog,    │
│ Direct2D/DirectWrite │                                 │ Finder (v2)              │
└─────────▲────────────┘                                 └──────────────────────────┘
          │ named pipe (stats stream)
┌─────────┴────────────┐
│ Harbor.Sensors.exe   │  C# .NET, non-elevated by default;
│ sensor backends      │  optional elevated mode for temperatures
└──────────────────────┘
```
- **Dock** owns: AppBar registration, taskbar hide/restore, window tracking, icons, rendering.
- **Shell** owns: settings persistence, UI for config, watchdog that restores the taskbar if the
  dock dies, starting/stopping other processes.
- **Sensors** owns: all hardware polling. Publishes snapshots; never blocks the dock.

## Inside each component: hexagonal core
- `core/` — pure domain logic, no Win32/WinRT includes (testable on any machine).
- `ports/` — interfaces (`IWindowSource`, `IIconProvider`, `ISensorBackend`, `IIpcChannel`…).
- `adapters/` — Win32/WinRT/PDH/vendor implementations.
- Composition root wires adapters at startup based on OS/arch capability detection.

## Key Win32 mechanisms (dock)
- AppBar: `SHAppBarMessage(ABM_NEW/ABM_SETPOS)` so maximized windows respect the dock.
- Window tracking: `SetWinEventHook` (EVENT_OBJECT_CREATE/DESTROY/SHOW/HIDE, EVENT_SYSTEM_FOREGROUND,
  EVENT_OBJECT_NAMECHANGE) + initial `EnumWindows`; filter with the "alt-tab eligible" rules
  (visible, unowned or WS_EX_APPWINDOW, not WS_EX_TOOLWINDOW, not cloaked via `DWMWA_CLOAKED`).
- App identity/grouping: AppUserModelID via `SHGetPropertyStoreForWindow`, fallback to exe path.
- Icons: `IShellItemImageFactory` (high-res), cache by AUMID/path + DPI.
- Visuals: DirectComposition visual tree, Direct2D for drawing, `DwmSetWindowAttribute`
  (`DWMWA_SYSTEMBACKDROP_TYPE`, `DWMWA_WINDOW_CORNER_PREFERENCE`) for backdrop/corners.
- Taskbar: hide `Shell_TrayWnd` + `Shell_SecondaryTrayWnd`; restore guaranteed (see CLAUDE.md rule 1).

## Sensor backends (Sensors)
| Metric | Baseline (both archs, no admin) | Optional/extra |
|---|---|---|
| CPU % (total, per core) | `GetSystemTimes`, PDH `Processor Information` | — |
| RAM | `GlobalMemoryStatusEx` | — |
| Storage | `GetDiskFreeSpaceExW` per volume | PDH disk throughput |
| GPU % / VRAM | PDH `GPU Engine`, `GPU Adapter Memory` | NVML (NVIDIA x64), ADLX (AMD x64) |
| Battery | `GetSystemPowerStatus`, `IOCTL_BATTERY_*` | — |
| Temps | ACPI thermal zone via WMI (unreliable, label as approximate) | Elevated LibreHardwareMonitorLib backend (x64 mainly) |
Every backend reports `available: false` instead of throwing.

## IPC
- Named pipes, per-user ACL, name includes session ID: `\\.\pipe\harbor.<channel>.<sessionId>`.
- Newline-delimited JSON, each message `{ "v": 1, "type": "...", "payload": {...} }`.
- Schemas in `contracts/*.schema.json`; contract tests on both C++ and C# sides.
- Stats stream: Sensors → Dock at 1 Hz (configurable), latest-value-wins (no queue buildup).

## Error handling
- Adapters convert OS errors (HRESULT/Win32) into domain results at the boundary.
- Dock never crashes on bad IPC input: validate, log, drop.
- Structured logs to `%LOCALAPPDATA%\Harbor\logs`, rolling.
