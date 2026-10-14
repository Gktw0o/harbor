# Harbor

A macOS-style dock and system-stats island for Windows 11, native on **x64 and ARM64**.

> **Status: M0 (skeleton).** An empty dock window and the Harbor Shell app exchange `ping`/`pong`
> over a per-user named pipe. Harbor does **not** hide or replace the taskbar yet.

## Prerequisites

- Windows 11 22H2 or later, x64 or ARM64.
- **Visual Studio 2026 or its Build Tools** (what local builds and CI use; 2022 may work but is
  untested), with these components:

  | Component | ID |
  |---|---|
  | MSVC x64/x86 build tools | `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` |
  | MSVC ARM64 build tools | `Microsoft.VisualStudio.Component.VC.Tools.ARM64` |
  | Windows 11 SDK (10.0.26100) | `Microsoft.VisualStudio.Component.Windows11SDK.26100` |
  | C++ CMake tools (CMake + Ninja) | `Microsoft.VisualStudio.Component.VC.CMake.Project` |
  | vcpkg package manager | `Microsoft.VisualStudio.Component.Vcpkg` |

  Add missing ones with the Visual Studio Installer, or for Build Tools:
  `setup.exe modify --installPath "<Build Tools path>" --add <ID>` (from `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer`).
  A standalone vcpkg works too: set `VCPKG_ROOT` to it.
- **.NET SDK 10.0.402 or later** (pinned in [`global.json`](global.json)): `winget install Microsoft.DotNet.SDK.10`.
- On an ARM64 PC, to also run the **x64** tests (under emulation), the x64 .NET runtime:
  `winget install Microsoft.DotNet.Runtime.10 --architecture x64`.
- For formatting C++: **clang-format 23.1.3**, the version CI uses (`winget install LLVM.LLVM`, or
  `pipx install clang-format==23.1.3`).

## Build and test

One command per architecture builds the dock (C++), the .NET solution, and runs every test, including an
end-to-end test that starts the real dock:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Arch x64
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Arch arm64
```

Options: `-Config Release` (default `Debug`), `-NoTest` (build only).

`build.ps1` enters the Visual Studio developer environment for the target architecture itself, so it runs
from any terminal. Where tests can run:

| Machine | x64 | arm64 |
|---|---|---|
| ARM64 PC | build + test (emulated) | build + test |
| x64 PC | build + test | build only (`-NoTest`): x64 hardware can't run ARM64 code |

### Individual steps

C++ (inside a [VS developer shell](https://learn.microsoft.com/visualstudio/ide/reference/command-prompt-powershell)
for the target architecture; CMake stops with an error if the shell and preset don't match):

```powershell
cmake --preset msvc-x64
cmake --build --preset msvc-x64-debug     # or msvc-x64-release
ctest --preset msvc-x64                   # add --build-config Release for Release
```

.NET (`-r`/`-a` don't work on solutions, so pick the architecture with the platform):

```powershell
dotnet build Harbor.slnx -c Debug -p:Platform=x64      # or -p:Platform=ARM64
dotnet test --solution Harbor.slnx -c Debug -p:Platform=x64 --no-build
```

Set `HARBOR_DOCK_EXE` to a built `harbor-dock.exe` of the same architecture to include the end-to-end test.

Formatting:

```powershell
clang-format -i <files>                                   # C++
dotnet format whitespace Harbor.slnx; dotnet format style Harbor.slnx   # C#
```

## Run

Start the Shell, then the dock (either order works; the dock reconnects on its own):

```powershell
src\Harbor.Shell\bin\x64\Debug\net10.0-windows10.0.26100.0\win-x64\Harbor.Shell.exe
build\msvc-x64\src\dock\Debug\harbor-dock.exe
```

The Shell shows `Dock connected · pong N · <round trip>`, and the dock's title shows the last ping.

## Layout

| Path | Contents |
|---|---|
| `src/dock/` | C++ dock: `core/` (portable logic), `ports/` (interfaces), `adapters/` (Win32), `app/` |
| `src/Harbor.Shell/` | WinUI 3 app: settings, onboarding and the dock's watchdog (later milestones) |
| `src/Harbor.Core/` | Shared C# code, including the IPC codec and pipe server |
| `src/Harbor.Sensors/` | Hardware sensors service (empty until milestone M4) |
| `contracts/` | IPC message schemas and shared test fixtures, see [contracts/README.md](contracts/README.md) |
| `tests/` | C++ (`dock/`) and C# (`Harbor.Core.Tests/`) tests |
| `docs/adr/` | Architecture decision records |

## License

[Functional Source License 1.1, Apache 2.0 future license](LICENSE) (FSL-1.1-ALv2).
