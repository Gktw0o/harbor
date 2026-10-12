# Harbor — project instructions for Claude Code

> "Harbor" is a working name. Rename freely (search-replace `Harbor` / `harbor`).

Harbor is a macOS-style **Dock + system-stats island** that replaces the Windows 11 taskbar,
with a **Finder-style file manager** planned for v2. It ships natively for **x64 and ARM64**.

My global `~/.claude/CLAUDE.md` engineering standards apply here and win on conflict.
This file only adds project-specific rules.

---

## Memory Bank (adapted from Cline's Memory Bank)

My memory resets between sessions. The Memory Bank is the only link to previous work,
so it must be kept accurate. Core files are imported below and load automatically:

@memory-bank/projectbrief.md
@memory-bank/productContext.md
@memory-bank/systemPatterns.md
@memory-bank/techContext.md
@memory-bank/activeContext.md
@memory-bank/progress.md

File hierarchy: `projectbrief` → (`productContext`, `systemPatterns`, `techContext`) → `activeContext` → `progress`.
Extra context (feature specs, IPC contracts, research spikes) goes in `memory-bank/` subfiles or `docs/adr/`,
and is read on demand, not imported.

### Workflow
1. **Plan mode first** for any milestone or change touching more than one component.
   Read `activeContext.md` + `progress.md`, state the plan, wait for approval.
2. **Act**: implement in small, buildable steps. Build both architectures before calling a step done.
3. **Update the Memory Bank** when:
   - a significant change lands, a new pattern is discovered, or a decision is made;
   - I say **"update memory bank"** — then review ALL six files, even ones that seem unchanged,
     focusing on `activeContext.md` and `progress.md`.
4. Keep Memory Bank files short and current: replace stale lines instead of appending history.
   Decisions with lasting rationale go to `docs/adr/NNNN-title.md`.

---

## Non-negotiable project rules

1. **Never leave the user without a taskbar.** Any code that hides `Shell_TrayWnd` /
   `Shell_SecondaryTrayWnd` must restore it on: normal exit, crash (unhandled-exception filter),
   `WM_ENDSESSION`, and via the watchdog in `Harbor.Shell`. A global panic hotkey
   (default `Ctrl+Alt+Shift+H`) restores it instantly. Test the restore path before the hide path.
2. **Both architectures, always.** Every PR builds `x64` and `arm64`. No x64-only dependency without
   an ARM64 fallback that degrades gracefully (show "N/A", never crash).
3. **No kernel drivers in v1.** Temperatures that need a driver are an optional, opt-in,
   elevated backend in `Harbor.Sensors` — never in the dock process.
4. **The dock process stays lean**: no .NET, no WebView, idle CPU < 0.5 %, working set < 40 MB.
   It renders; it does not poll hardware.
5. **IPC is versioned.** Any change to a message in `contracts/` bumps its version and updates
   both sides plus contract tests in the same change.
6. **Undocumented shell internals** (tray icon scraping, explorer hooks, DLL injection) are forbidden
   unless an ADR approves a spike for it.
7. Per-monitor-v2 DPI awareness, multi-monitor and light/dark theme are requirements, not polish.

## Commands
(Fill in as they become real; keep this list accurate.)
- Configure + build dock: `cmake --preset msvc-x64 && cmake --build --preset msvc-x64-debug`
  (and `msvc-arm64`)
- Build .NET: `dotnet build Harbor.sln -c Debug -r win-x64` (and `win-arm64`)
- Tests: `ctest --preset msvc-x64` · `dotnet test`
- Format: `clang-format -i` (C++) · `dotnet format` (C#)
