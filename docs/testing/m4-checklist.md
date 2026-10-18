# M4 overlay dock and island checklist (manual)

The automated tests run the real dock in test mode (a stand-in taskbar, off-screen test windows, test hooks for
full screen) and Harbor Shell in test mode, whose power actions, media and controls are stand-ins that change
nothing on the PC. In CI they also open the island with the dock's Start button, search, launch, and toggle
the made-up controls. `scripts\safety-check.ps1` adds the real taskbar and a real full-screen window on one PC.
This checklist covers what needs the real system: real apps and games, volume, brightness, radios, the theme,
the Settings app, power, a second monitor and a screen reader.

The dock's Start button opens the island; the Windows key keeps opening Windows' own Start (not part of M4, see
ADR 0015's note).

**Before you start**
- Build Release: `scripts\build.ps1 -Arch <x64|arm64> -Config Release`, then start `Harbor.Shell.exe` from the
  Release output. "Hide on" means turning on **Hide the Windows taskbar**.
- If anything goes wrong: **Ctrl+Alt+Shift+H** (it also hides the dock until hiding is turned on again).
  **Stop dock** in Harbor Shell closes the dock.
- Section 6 changes real settings (volume, brightness, Wi-Fi, Bluetooth, the theme) and section 7 locks or
  sleeps the PC: note your settings first, and save your work before 7.

## 1. Run the script on this PC

`powershell -ExecutionPolicy Bypass -File scripts\safety-check.ps1 -Arch <arch> -ExerciseAutoHide`. M4 adds
`tuck-panic`, `tuck-kill`, `tuck-close-shell`, `tuck-stuck`, `tuck-edge` and `island-kill`: a black window
covers the primary monitor for a few seconds in each tuck scenario, and `tuck-edge` moves the mouse. The
`appbar-*` and `stop-dock` scenarios now check that the work area never shrinks (no strip). Then
`-Scenario island-budget` (about 2.5 min: Harbor Shell's CPU and memory with the island collapsed, open and
closed again, and how fast Start brings it in front). Paste the result tables below.

## 2. The overlay dock

| # | Steps | Expected |
|---|---|---|
| 2.1 | Hide on. Maximize a window (File Explorer, Edge). | The dock floats over the bottom of the window; the window fills the whole screen behind it (no strip is kept free). |
| 2.2 | Play a video full screen (Edge, YouTube, F key) or start a game in borderless full screen. | After about 2.5 s the dock goes behind it. The taskbar stays hidden. |
| 2.3 | While it is full screen, rest the mouse on the bottom edge. | After a moment the dock comes back over the video; moving away, it goes behind again after about a second. |
| 2.4 | Leave full screen (Esc), or Alt+Tab to another window. | The dock is back on top within about a second. |
| 2.5 | A game in exclusive full screen (if you have one). | The game runs as before (no stutter from the dock); the dock is back after leaving it. |
| 2.6 | Energy saver on (quick settings or Settings > Power), then off. | The dock and the island turn solid while it is on, glass again after. |

## 3. Start button

| # | Steps | Expected |
|---|---|---|
| 3.1 | Click the dock's first button (Start). | The island opens at the top centre with the search box focused. |
| 3.2 | Close Harbor Shell (the dock keeps running), click Start. | Windows' Start opens (Harbor Shell isn't there to open the island). Start Harbor Shell again. |
| 3.3 | Open the island, click **Windows Start**. | The island closes and Windows' Start opens. |

## 4. The island

| # | Steps | Expected |
|---|---|---|
| 4.1 | Look at the top centre of the dock's monitor. | A small glass notch on the top edge, empty. |
| 4.2 | Move the mouse to it. | It grows like a bubble; moving away, it shrinks back. |
| 4.3 | Click it. | It opens: search box, then Stats, Power, Media and Controls side by side. |
| 4.4 | Press Esc; open it again and click elsewhere. | It closes both times. |
| 4.5 | Light and dark mode, high contrast. | Readable in each; the glass follows the mode. |
| 4.6 | Move the dock to a second monitor with another scale (resting on its bottom edge). | The island follows to the top of that monitor, sharp at its scale. |
| 4.7 | A full-screen video, then the panic hotkey. | The island is hidden behind full-screen apps and after the panic hotkey (until hiding is turned on again). |

## 5. Apps, stats, media

| # | Steps | Expected |
|---|---|---|
| 5.1 | Open the island, type part of an app's name ("calc", "term", a Turkish name with ı/İ if you have one). | Matching apps show as you type; Enter starts the first, a click starts another. |
| 5.2 | Install or remove an app, wait a minute, search again. | The list follows. |
| 5.3 | Look at Stats for a while. | CPU, cores, memory, GPU, battery (laptop) and drives move with what the PC does. |
| 5.4 | Play music in Spotify, the Media Player or a browser. | Title, artist, artwork and progress show; previous / play-pause / next work; the progress moves. |
| 5.5 | Pause it, close the player. | The section follows; "Nothing playing" at the end. |

## 6. Controls (change real settings: note yours first)

| # | Steps | Expected |
|---|---|---|
| 6.1 | Drag the volume slider; click mute; change the volume with the keyboard's keys while the island is open. | The PC's volume follows the slider without jumps; mute works; the slider follows the keys. |
| 6.2 | Laptop: drag the brightness slider. Desktop: look for it. | The built-in display follows. A desktop (or an external display only) shows no brightness slider. |
| 6.3 | Turn Wi-Fi off and on, then Bluetooth. | Each turns off and on (Windows' quick settings agree). If Windows refuses, the toggle goes back by itself. |
| 6.4 | Turn dark mode on and off. | Windows and apps switch modes within a few seconds; Settings > Personalization > Colors agrees. |
| 6.5 | Click Focus, Night light, Airplane mode, Power mode, one at a time. | The island closes and Settings opens on that page each time. |

## 7. Power (save your work first)

| # | Steps | Expected |
|---|---|---|
| 7.1 | Lock. | The PC locks at once; after signing in, the island is closed. |
| 7.2 | Sleep (a laptop with modern standby: the displays turn off). | The PC sleeps; it wakes as usual; the taskbar is still hidden by the dock, or back if hiding was off. |
| 7.3 | Restart and Shut down: click once. | Each asks first; Cancel cancels. (Optional: confirm a restart; the taskbar is back after it, Harbor isn't started yet: no autostart until M5.) |
| 7.4 | Sign out (optional). | Asks first; after signing in again the taskbar is there. |

## 8. Screen reader

| # | Steps | Expected |
|---|---|---|
| 8.1 | Narrator on. Open the island with the Start button, Tab through it. | The search box, results, each section's controls (sliders with their values, toggles with on/off, buttons) are read with their names. |

## Results

| Date | PC (arch, Windows build) | Section | Result | Notes |
|---|---|---|---|---|
| 2026-10-17 | dev PC, ARM64, Windows 11 10.0.28000, one monitor, Release | 1 (23 scenarios) | 22 PASS, 1 SKIP | island-move SKIP (one monitor). panic, peek-kill and menu-panic first failed in the script itself (UI Automation missed the hide toggle once right after the Shell opened; hiding never came on); fixed (Enable-Hide waits for it) and rerun: panic restored in 81 ms, peek-kill 292 ms, menu-panic 236 ms. tuck-*: tucked 2.8-3.5 s after a full-screen window came, the taskbar stayed hidden while tucked; panic restored in 74 ms, dock killed 231 ms, Shell closed 238 ms; full screen ending unannounced: on top again in 240 ms; the bottom edge brought it back in 517 ms, tucked again 1.7 s after the mouse left. island-kill: island in front 330 ms after Start, restored 285 ms after the Shell was killed. The overlay keeps no strip (work area 0 px short). Idle dock: 0.009 % CPU, 31.3 MB working set, 8.6 MB private. |
| 2026-10-17 | same | 1 (island-budget) | PASS, then fixed | Open: +13.6 MB private, +11.3 MB 35 s after closing; Start -> island in front 282 ms cold, 11 ms warm. Collapsed: 0.289 % of all CPUs (about 3.5 % of one core) — not "about 0": background JIT in the first minute (the scenario measured from 10 s) and the main window redrawing "pong N" every second since M0. Now the text changes only when the link comes or goes and the scenario waits 60 s: an idle test Shell's threads went from ~43 to ~10 M cycles a second. |
| 2026-10-18 | same, docked: two monitors (1440x900 at 192 dpi + 1920x1080) | 1 (island-move, island-budget) | 2 PASS | island-move: there in 60 ms, back in 78 ms, the taskbar stayed hidden. island-budget after the fix: collapsed 0.020 % of all CPUs over 30 s; open +18.8 MB private, +10.6 MB 35 s after closing; Start -> island in front 384 ms first, 5 ms again. |
