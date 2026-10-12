# Tech Context

## Languages & frameworks
- **Dock**: C++20, MSVC, Win32 + DirectComposition + Direct2D/DirectWrite, WIL (`wil/`) for RAII
  and error handling, C++/WinRT only where a WinRT API is needed.
- **Shell / Sensors**: C# on the current .NET LTS, WinUI 3 (Windows App SDK) for Shell UI,
  CommunityToolkit.Mvvm.
- Target OS: Windows 11 22H2+ (needed for system backdrop APIs).

## Build
- C++: CMake + Ninja, `CMakePresets.json` with `msvc-x64` and `msvc-arm64` presets; vcpkg manifest
  mode for deps (wil, nlohmann-json or simdjson, gtest/Catch2).
- .NET: SDK-style projects, `RuntimeIdentifiers` = `win-x64;win-arm64`.
- CI: GitHub Actions matrix on x64 and ARM64 Windows runners; build + unit tests on both.
- Packaging (later milestone): MSIX containing all three executables, with full-trust dock.

## Repo layout
```
/src/dock/            C++ dock (core/, ports/, adapters/, app/)
/src/Harbor.Shell/    C# WinUI 3 app
/src/Harbor.Sensors/  C# sensors service
/src/Harbor.Core/     shared C# domain + IPC contracts
/contracts/           JSON Schemas for IPC messages
/tests/               C++ and C# tests, contract tests
/docs/adr/            architecture decision records
/memory-bank/         this memory bank
```

## Dev environment
- VS Code + Claude Code; extensions: C/C++ (or clangd), CMake Tools, C# Dev Kit.
- Visual Studio Build Tools with MSVC x64 **and ARM64** toolsets + Windows 11 SDK.
- Full Visual Studio optional, mainly for WinUI 3 XAML Hot Reload and the native debugger.
- Test hardware: one x64 machine and one real ARM64 laptop (emulation hides perf issues).

## Constraints / gotchas
- WinRing0-based temperature access is flagged by Defender; do not ship it.
- On ARM64, NVML/ADLX don't apply; CPU temps may be unavailable entirely.
- Hiding the taskbar does not stop Explorer from re-showing it on some events
  (explorer restart, display change) — the dock must re-assert on `TaskbarCreated` message.
- Tray (notification area) icons have no public read API — open research item.
