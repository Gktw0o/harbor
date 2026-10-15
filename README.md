# Harbor

A macOS-style dock and system-stats island for Windows 11, native on **x64 and ARM64**.

> **Status: M2 (the dock window).** A rounded island at the bottom of the primary screen shows your pinned
> apps and starts them with a click; maximized windows stop above it; screen readers see its apps. Harbor can
> hide the Windows taskbar and gives it back on exit, crash, hang, sign-out, or **Ctrl+Alt+Shift+H**. Running
> windows, other monitors and stats come next (M3, M4).

## If your taskbar is ever missing

1. Press **Ctrl+Alt+Shift+H** (works while Harbor's dock runs, even if it is hung). It also hides the dock
   until you turn hiding on again.
2. Or click **Restore taskbar now** (or **Stop dock**) in Harbor Shell.
3. Or press Ctrl+Shift+Esc, select **Windows Explorer** under Processes, and choose **Restart**.

Harbor turns on Windows' "Automatically hide the taskbar" while it hides the taskbar, if it was off, and
turns it off again when it gives the taskbar back. If that ever didn't happen (say Harbor crashed and was
never started again), turn it off under Settings > Personalization > Taskbar > Taskbar behaviors, or just
start Harbor Shell once: it restores the setting from its record. Logs are in `%LOCALAPPDATA%\Harbor\logs`.
How this works: [ADR 0006](docs/adr/0006-hiding-the-taskbar.md), [ADR 0007](docs/adr/0007-taskbar-safety.md).

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

Set `HARBOR_DOCK_EXE` to a built `harbor-dock.exe` of the same architecture to include the end-to-end tests,
and `HARBOR_MARKER_EXE` / `HARBOR_UIA_CLIENT_EXE` to `harbor_test_marker.exe` / `harbor_test_uia_client.exe`
from `build\msvc-<arch>\tests\dock\<Config>\` for the launch and screen-reader tests (`build.ps1` sets all
three). Tests never touch your real taskbar or work area: they use stand-in windows and an AppBar that
reserves nothing. Their docks do show a small island at the bottom of the screen for a moment.

Real-taskbar check (local only, never CI; your taskbar disappears for seconds at a time while it runs):

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Arch arm64 -Config Release
powershell -ExecutionPolicy Bypass -File scripts\safety-check.ps1 -Arch arm64 -ExerciseAutoHide
```

It also checks the dock's screen strip (AppBar) and "Stop dock". The rest, by hand:
[docs/testing/m1-safety-checklist.md](docs/testing/m1-safety-checklist.md) and
[docs/testing/m2-checklist.md](docs/testing/m2-checklist.md).

Formatting:

```powershell
clang-format -i <files>                                   # C++
dotnet format whitespace Harbor.slnx; dotnet format style Harbor.slnx   # C#
```

## Run

Start Harbor Shell; it starts the dock built next to it and watches over it:

```powershell
src\Harbor.Shell\bin\x64\Debug\net10.0-windows10.0.26100.0\win-x64\Harbor.Shell.exe
```

The dock reserves a strip at the bottom of the primary screen and shows the apps pinned in
`%LOCALAPPDATA%\Harbor\settings.json` (first run: File Explorer, Microsoft Edge, Windows Terminal,
Settings). **Edit pinned apps…** opens the file; Harbor picks up your changes when you save it:

```json
{ "v": 1, "pinned": [ { "app": "Microsoft.Windows.Explorer" }, { "path": "C:\\Tools\\app.exe" } ] }
```

An `app` is an AppUserModelID (`Get-StartApps` in PowerShell lists them), a `path` an absolute path to an
`.exe` or `.lnk`. Turn on **Hide the Windows taskbar** to hide it. The Shell shows the link to the dock
(`pong N · <round trip>`) and the taskbar's state, starts the dock again if it fails, and **Stop dock** closes
it. A dock you start yourself (`build\msvc-x64\src\dock\Debug\harbor-dock.exe`) is adopted when it connects.
How the dock works: [ADR 0008](docs/adr/0008-dock-rendering.md), [ADR 0009](docs/adr/0009-pinned-apps-launching-appbar.md).

## Layout

| Path | Contents |
|---|---|
| `src/dock/` | C++ dock: `core/` (portable logic), `ports/` (interfaces), `adapters/` (Win32), `app/` |
| `src/Harbor.Shell/` | WinUI 3 app: starts and watches the dock, pinned apps; settings UI and onboarding later |
| `src/Harbor.Core/` | Shared C# code: IPC codec and pipe server, the dock's watchdog, taskbar restore, settings, app lookup and icons, logs |
| `src/Harbor.Sensors/` | Hardware sensors service (empty until milestone M4) |
| `contracts/` | IPC message, state-file and icon-file formats with shared test fixtures, see [contracts/README.md](contracts/README.md) |
| `tests/` | C++ (`dock/`) and C# (`Harbor.Core.Tests/`) tests |
| `scripts/` | `build.ps1` (build and test one architecture), `safety-check.ps1` (real-taskbar check) |
| `docs/` | Architecture decision records (`adr/`), manual test checklists (`testing/`) |

## License

[Functional Source License 1.1, Apache 2.0 future license](LICENSE) (FSL-1.1-ALv2).
