# M2 dock checklist (manual)

The automated tests run the real dock in test mode: it draws its island, launches apps and answers screen
readers, but its AppBar reserves nothing and its "taskbar" is a pair of stand-in windows.
`scripts\safety-check.ps1` adds the real AppBar on one PC (the strip is freed when the dock dies, one strip
after a restart, "Stop dock"). This checklist covers what needs eyes, hardware or a screen reader.

**Before you start**
- Build Release: `scripts\build.ps1 -Arch <x64|arm64> -Config Release`, then start `Harbor.Shell.exe` from the
  Release output. "Hide on" means turning on **Hide the Windows taskbar**.
- If anything goes wrong: **Ctrl+Alt+Shift+H** (it also hides the dock until hiding is turned on again).
  **Stop dock** in Harbor Shell closes the dock.
- The pinned apps are in `%LOCALAPPDATA%\Harbor\settings.json` (**Edit pinned apps…**). The first run writes
  File Explorer, Microsoft Edge, Windows Terminal and Settings; apps that aren't installed are left off.

## 1. Run the script on this PC

`powershell -ExecutionPolicy Bypass -File scripts\safety-check.ps1 -Arch <arch> -ExerciseAutoHide`. It now
includes `appbar-kill`, `appbar-restart` and `stop-dock`, and `idle` measures the dock with its island drawn.
Paste the result table below.

## 2. Looks

| # | Steps | Expected |
|---|---|---|
| 2.1 | Dark mode, transparency on. | A rounded island at the bottom centre, blurred background with a dark tint, a faint outline, the apps' icons. |
| 2.2 | Light mode (Settings > Personalization > Colors). | Turns light within a second, without restarting. |
| 2.3 | Transparency effects off. | Opaque island, no black corners or background. |
| 2.4 | Energy saver on (laptop, or Settings > System > Power). | Opaque, like 2.3; back to blur when it's off. |
| 2.5 | High contrast on (a contrast theme). | The theme's colors: window background, text-colored outline, highlight on hover. |
| 2.6 | Hover over each icon, then leave the island. | A highlight follows the mouse; it goes when the mouse leaves. |
| 2.7 | Click an icon. | The app starts and comes to the front. The dock never takes the focus (the window you were in stays active until the app opens). |
| 2.8 | Remove an app from settings.json, save; then break the file (delete a bracket), save. | The icon goes; with the broken file, Harbor Shell shows the problem and the dock keeps the last good list. |

## 3. The strip (AppBar) and other windows

| # | Steps | Expected |
|---|---|---|
| 3.1 | Hide off: maximize a window. | Its bottom edge stops above the dock's strip, which sits above the taskbar. |
| 3.2 | Hide on: maximize a window. | Its bottom edge stops above the strip, now at the bottom of the screen. |
| 3.3 | Chrome or Edge maximized. | No 1-2 px gap or overlap at the strip's edge (note any). |
| 3.4 | A full-screen video (F11 in a browser, or a game). | The dock is behind it; it comes back on top when the video leaves full screen. |
| 3.5 | Taskbar set to auto-hide in Windows, hide off. | The taskbar slides over the strip when the mouse reaches the bottom edge; note whether that's awkward. |
| 3.6 | Restart Explorer (Task Manager > Windows Explorer > Restart). | The dock keeps its place; maximized windows still stop above it. |
| 3.7 | Stop dock. | The strip goes: maximized windows reach the taskbar (or the bottom) again. |

## 4. Scale and monitors

| # | Steps | Expected |
|---|---|---|
| 4.1 | Change the primary display's scale (100, 150, 200 %). | The island keeps its size in DIPs, crisp, at the bottom centre, within a second or two. |
| 4.2 | Change the resolution. | Same. |
| 4.3 | Second monitor: make it the primary display, then switch back. | The dock moves to the new primary monitor. |

## 5. Screen reader

| # | Steps | Expected |
|---|---|---|
| 5.1 | Narrator on (Ctrl+Win+Enter); scan mode or Caps Lock + arrows to the dock. | "Harbor Dock, tool bar", then each app as a button by name. |
| 5.2 | Invoke an app from Narrator (Caps Lock + Enter). | It starts. |
| 5.3 | Accessibility Insights for Windows (if installed): inspect the island. | Tool bar with one button per app, each with Invoke; no errors. |

## 6. Recovery

| # | Steps | Expected |
|---|---|---|
| 6.1 | Kill harbor-dock.exe in Task Manager. | The taskbar comes back; Harbor Shell starts the dock again within a few seconds (hiding stays off). |
| 6.2 | Kill it four times within a minute. | After the third restart Harbor Shell gives up and says so. |
| 6.3 | Hide on, Ctrl+Alt+Shift+H. | Taskbar back, dock gone; turning hiding on again brings back both. |

## 7. Budget on the x64 PC

`safety-check.ps1 -Arch x64 -Scenario idle` on the x64 machine: under 0.5 % CPU and 40 MB working set.

## Results

| Date | PC (arch, Windows build) | Section | Result | Notes |
|---|---|---|---|---|
