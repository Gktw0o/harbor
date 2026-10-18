# Harbor

A macOS-style dock and a top-of-screen island for Windows 11, native on **x64 and ARM64**.

> **Status: M4 (overlay dock and the island), in progress.** The dock floats at the bottom of the screen with
> your pinned and running apps, a Start button and the tray; it keeps no strip of the screen and goes behind
> full-screen apps. The island at the top of the screen opens into app search, stats, power, media and quick
> controls. Harbor can hide the Windows taskbar and gives it back on exit, crash, hang, sign-out, or
> **Ctrl+Alt+Shift+H**. The Windows key doesn't open the island yet.

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
and `HARBOR_MARKER_EXE` / `HARBOR_UIA_CLIENT_EXE` / `HARBOR_TEST_WINDOWS_EXE` to `harbor_test_marker.exe` /
`harbor_test_uia_client.exe` / `harbor_test_windows.exe` from `build\msvc-<arch>\tests\dock\<Config>\` for
the launch, screen-reader and running-window tests, and `HARBOR_SHELL_EXE` to a built `Harbor.Shell.exe` for
Harbor Shell's own end-to-end tests (`build.ps1` sets all five). Tests never touch your real taskbar, work area
or apps: they use stand-in windows, an AppBar that reserves nothing, and off-screen test windows that only
their docks follow. Test Shells get stand-ins for power, media and the quick controls, so a test never sleeps
the PC or changes your volume, display, radios or theme; their windows stay off screen and out of Alt+Tab.
Their docks and islands do show at the edges of the screen for a moment. A few tests move the focus between
windows; they run in CI, and locally only with `HARBOR_FOREGROUND_TESTS=1` (otherwise they are skipped).

Real-taskbar check (local only, never CI; your taskbar disappears for seconds at a time while it runs):

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Arch arm64 -Config Release
powershell -ExecutionPolicy Bypass -File scripts\safety-check.ps1 -Arch arm64 -ExerciseAutoHide
```

It also checks that the dock reserves no screen strip (its AppBars), "Stop dock", the tray button's peek,
attention marks, menus and moving between monitors (it moves the mouse for those), and every way back while
the dock is tucked behind a full-screen app (a black window covers the primary monitor for a few seconds);
`-Scenario budget` measures the dock's memory with 30 running apps, `-Scenario island-budget` Harbor Shell's
with the island. The rest, by hand: [docs/testing/m1-safety-checklist.md](docs/testing/m1-safety-checklist.md),
[docs/testing/m2-checklist.md](docs/testing/m2-checklist.md), [docs/testing/m3-checklist.md](docs/testing/m3-checklist.md)
and [docs/testing/m4-checklist.md](docs/testing/m4-checklist.md).

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

The dock floats over the bottom of the screen (it reserves no space: maximized windows reach the bottom edge
under it), starts on the primary one, and shows the apps pinned in
`%LOCALAPPDATA%\Harbor\settings.json` (first run: File Explorer, Microsoft Edge, Windows Terminal,
Settings). **Edit pinned apps…** opens the file; Harbor picks up your changes when you save it:

```json
{ "v": 1, "pinned": [ { "app": "Microsoft.Windows.Explorer" }, { "path": "C:\\Tools\\app.exe" } ] }
```

An `app` is an AppUserModelID (`Get-StartApps` in PowerShell lists them), a `path` an absolute path to an
`.exe` or `.lnk`. Turn on **Hide the Windows taskbar** to hide it. The Shell shows whether the dock is
connected and the taskbar's state, starts the dock again if it fails, and **Stop dock** closes it. A dock you start yourself (`build\msvc-x64\src\dock\Debug\harbor-dock.exe`) is adopted when it connects.
How the dock works: [ADR 0008](docs/adr/0008-dock-rendering.md), [ADR 0009](docs/adr/0009-pinned-apps-launching-appbar.md).

### Using the dock

- **Start**, the first button, opens the island on app search (or Windows' Start while Harbor Shell isn't
  running). **Windows Start** in the island opens Windows' own.
- **Full screen**: about 2.5 s after a video or a game goes full screen, the dock goes behind it; rest the mouse
  on the bottom edge to bring it back.
- **Running apps** have a dot under their icon; those that aren't pinned come after a divider, in the order
  you started them. Click one to bring its most recent window forward, again for its next window; with one
  window in front, a click minimizes it (another restores it).
- **Right-click** an app for its windows, a new window, pin or unpin, and closing its windows. Rest the mouse on
  an item for its name. Apps that don't fit go behind the **…** button.
- An app that **flashes** its taskbar button (a new message, a finished download) gets a highlight on the dock
  until you bring it forward.
- **Other monitors**: rest the mouse on a monitor's bottom edge and the dock moves there.
- **The tray**: while Harbor hides the taskbar, the chevron button at the dock's end brings the taskbar back
  until you click elsewhere: tray icons, the clock, quick settings. **Win+N** (notifications and calendar) and
  **Win+A** (quick settings) work too.

Running windows, monitors and the tray: [ADR 0010](docs/adr/0010-running-windows.md),
[ADR 0011](docs/adr/0011-monitors-and-attention.md), [ADR 0012](docs/adr/0012-tray.md). The overlay, full
screen and Start: [ADR 0014](docs/adr/0014-overlay-dock.md), [ADR 0015](docs/adr/0015-windows-key-and-start.md).

### Using the island

Harbor Shell shows a small glass notch at the top centre of the dock's monitor. It grows when the mouse comes
near; click it (or the dock's Start button) to open it, and press Esc or click elsewhere to close it.

- **Search**: type part of an app's name; Enter starts the first match, a click another.
- **Stats**: CPU (and each core), memory, GPU, battery and drives, read only while the island is open.
- **Power**: lock, sign out, sleep, hibernate (where enabled), restart, shut down; the last three ask first.
- **Media**: what is playing, with previous / play-pause / next.
- **Controls**: volume and mute, brightness (built-in displays), Wi-Fi, Bluetooth and dark mode, each only where
  the PC has it, and links to the Focus, Night light, Airplane mode and Power mode settings.

How it works: [ADR 0013](docs/adr/0013-the-island.md), [ADR 0016](docs/adr/0016-system-data-and-search.md).

## Layout

| Path | Contents |
|---|---|
| `src/dock/` | C++ dock: `core/` (portable logic), `ports/` (interfaces), `adapters/` (Win32), `app/` |
| `src/Harbor.Shell/` | WinUI 3 app: starts and watches the dock, pinned apps, the island; settings UI and onboarding later |
| `src/Harbor.Core/` | Shared C# code: IPC codec and pipe server, the dock's watchdog, taskbar restore, settings, app lookup and icons, the island's logic (search, stats, power, media, controls) with its Win32 adapters, logs |
| `src/Harbor.Platform/` | WinRT and composition adapters for Harbor Shell: the island's window, media sessions, radios |
| `src/Harbor.Sensors/` | Hardware sensors service (empty until temperatures, M6) |
| `contracts/` | IPC message, state-file and icon-file formats with shared test fixtures, see [contracts/README.md](contracts/README.md) |
| `tests/` | C++ (`dock/`, with test helpers: stand-in apps, windows and a screen-reader client) and C# (`Harbor.Core.Tests/`) tests |
| `scripts/` | `build.ps1` (build and test one architecture), `safety-check.ps1` (real-taskbar check) |
| `docs/` | Architecture decision records (`adr/`), manual test checklists (`testing/`) |

## License

[Functional Source License 1.1, Apache 2.0 future license](LICENSE) (FSL-1.1-ALv2).
