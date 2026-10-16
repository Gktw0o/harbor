# M3 running windows checklist (manual)

The automated tests run the real dock in test mode against off-screen test windows (`harbor_test_windows`),
a stand-in taskbar and a bare pipe server or Harbor Shell's supervisor. They cover grouping, late
AppUserModelIDs, dots and the divider, the click cycle, menus, the label, the overflow menu, attention marks,
the tray button's peek and every restore path while peeking. `scripts\safety-check.ps1` adds the real taskbar
on one PC. This checklist covers what needs real apps, eyes, a second monitor or a screen reader.

**Before you start**
- Build Release: `scripts\build.ps1 -Arch <x64|arm64> -Config Release`, then start `Harbor.Shell.exe` from the
  Release output. "Hide on" means turning on **Hide the Windows taskbar**.
- If anything goes wrong: **Ctrl+Alt+Shift+H** (it also hides the dock until hiding is turned on again).
  **Stop dock** in Harbor Shell closes the dock.

## 1. Run the script on this PC

`powershell -ExecutionPolicy Bypass -File scripts\safety-check.ps1 -Arch <arch> -ExerciseAutoHide`. M3 adds
`peek`, `peek-panic`, `peek-kill`, `attention`, `menu-panic` and `island-move` (two monitors; SKIP with one);
they move the mouse and click Harbor Shell's title bar, so leave the mouse alone while they run. Then the
budget: `-Scenario budget` (about three minutes: 30 test apps, a label and a menu, 50 monitor moves, 10,000
windows coming and going). Paste the result tables below.

## 2. Running apps

| # | Steps | Expected |
|---|---|---|
| 2.1 | Open File Explorer, Settings, Calculator, Windows Terminal, Edge, VS Code (or another Electron app). | Each appears once: on its pin with a dot, or after the divider in the order you opened them, with its name and icon within a second. |
| 2.2 | Two Edge (or Chrome) profiles, and an installed web app (PWA). | Each profile and the PWA are separate items, with their own names and icons (allow a few seconds). |
| 2.3 | Click a running app that isn't in front. | Its most recent window comes forward. |
| 2.4 | Click it again (two windows open). | The other window comes forward; again, back to the first. |
| 2.5 | One window, in front: click it. | It minimizes; another click restores it. |
| 2.6 | The same with an app run as administrator (Terminal elevated). | Minimizes and restores too. |
| 2.7 | Close all of an app's windows. | Its dot goes (pinned), or its item goes (not pinned). |
| 2.8 | A window on another virtual desktop (Win+Ctrl+D, open an app, switch back). | The app isn't on the dock while its only window is on the other desktop. Switching desktops leaves the island where it is, and the taskbar stays hidden (hide on). |
| 2.9 | An app that stops responding (any test app that hangs; or skip). | It stays on the dock while Windows shows its ghost window. |

## 3. Menus, label, overflow

| # | Steps | Expected |
|---|---|---|
| 3.1 | Right-click a running app. | Its name, its windows by title, New window, Pin to dock, Close window/Close all windows, in the island's colors. |
| 3.2 | Choose a window from the menu. | That window comes forward. |
| 3.3 | Pin to dock; then Unpin from dock (right-click the pin). | The app moves to the pinned items (settings.json gains it); unpinning puts it back after the divider. |
| 3.4 | Close all windows (an app with unsaved work, like Notepad). | Each window is asked to close; the app's own "save?" prompt appears; nothing is forced. |
| 3.5 | High contrast on: right-click. | A plain system menu in the theme's colors. |
| 3.6 | Rest the mouse on an item. | Its name above it after a moment; clicks go through the label; it goes when the mouse leaves. |
| 3.7 | Open many apps until they don't fit (or set a large scale). | A "…" button in the last place (before the tray button); its menu lists the rest with icons and starts or brings them forward. |

## 4. Attention

| # | Steps | Expected |
|---|---|---|
| 4.1 | An app that flashes its taskbar button (a Teams or WhatsApp message while another window is in front, or a download finishing in a background browser). | A highlight behind its item until you bring it forward. |
| 4.2 | Restore a minimized window with a click on the dock. | No highlight for it (Windows flashes windows restored this way; the dock ignores that). |

## 5. Monitors (second monitor)

| # | Steps | Expected |
|---|---|---|
| 5.1 | Hide on. Maximize a window on each monitor. | Each stops above a strip at the bottom of its monitor; the island is on the primary. |
| 5.2 | Rest the mouse on the other monitor's bottom edge. | The island moves there within half a second; maximized windows don't change size; the taskbar stays hidden. |
| 5.3 | Move the mouse quickly along the bottom, or drag a window across. | The island doesn't follow until the mouse rests there (not while dragging). |
| 5.4 | A full-screen video on the monitor without the island; rest on its bottom edge. | The island stays where it is. |
| 5.5 | Unplug the monitor with the island (or turn it off in Display settings). | The island goes back to the primary; the taskbar may show for a moment at most, then hides again. |
| 5.6 | Different scales on the two monitors (100 % and 200 %). | The island has the same size in DIPs on both and is sharp. |

## 6. Tray (peek)

| # | Steps | Expected |
|---|---|---|
| 6.1 | Hide on: the chevron button at the island's end. | The taskbar slides in with the tray icons and the clock. |
| 6.2 | Click a tray icon, the clock, quick settings. | Each opens normally. |
| 6.3 | Click elsewhere. | The taskbar slides away and stays hidden. |
| 6.4 | Win+N and Win+A with hiding on (no peek). | Notifications/calendar and quick settings open. |
| 6.5 | Hide off. | No chevron button. |

## 7. Screen reader

| # | Steps | Expected |
|---|---|---|
| 7.1 | Narrator on (Ctrl+Win+Enter); move to the dock. | Running apps say "running, N windows" (and "needs attention" when flashing); the "…" button says how many apps are behind it and opens with Caps Lock + Enter; the tray button says "Show taskbar". |
| 7.2 | Right-click menu with Narrator on (Shift+F10 is not wired yet: use the mouse). | Narrator reads each item. |

## Results

| Date | PC (arch, Windows build) | Section | Result | Notes |
|---|---|---|---|---|
| 2026-10-16 | ARM64 (Snapdragon), 26H1 build 28000, 2880x1800 at 200 % + 1920x1080 at 100 % | 1 (`-ExerciseAutoHide`) | 13 PASS, 4 FAIL | M1/M2 scenarios as before (panic 85 ms, dock killed 465 ms, frozen 6.5 s, Shell closed 242 ms); menu-panic: menu closed, restored in 271 ms; island-move: there in 41 ms, back in 70 ms, no restore meanwhile. idle FAIL: 41.8 MB working set, 21.0 MB private, 0.012 % CPU. peek, peek-panic, peek-kill and attention failed in the script itself (a variable shadowed by Wait-Until's parameter; the UTF-8 BOM Windows PowerShell puts on redirected input): fixed, to run again. |
| 2026-10-16 | same | 1 (`-Scenario budget`) | FAIL | Your apps: 42.6 MB / 24.2 MB private; 30 apps: 90.2 / 80.1; after a label and a menu 86.4 / 71.0; after 50 moves the same; after 10,000 windows 86.1 / 70.3 (no growth); 288-315 handles, 0-13 GDI, 30-37 USER; 0.012 % CPU at rest with 30 apps. Cause: each 256 px icon handed to Direct2D on WARP kept ~1.5 MB; the renderer now scales icons down first (a test dock with 30 icon apps: 75 -> 34 MB working set, 55 -> 9 MB private). To run again. |
