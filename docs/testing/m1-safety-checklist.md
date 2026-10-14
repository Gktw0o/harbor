# M1 safety checklist (manual)

The automated tests use stand-in taskbar windows and never touch the real taskbar.
`scripts\safety-check.ps1` covers the real taskbar on one PC: the panic hotkey, killing or freezing the
dock, closing or killing Harbor Shell, killing both, the Start menu, idle cost, and an Explorer restart.
This checklist covers what a script can't: other hardware, monitors, and the session itself.

**Before you start**
- Build Release: `scripts\build.ps1 -Arch <x64|arm64> -Config Release`.
- Note your taskbar settings (auto-hide on or off, taskbar on all displays).
- If anything goes wrong: **Ctrl+Alt+Shift+H**. If the taskbar is still missing, press Ctrl+Shift+Esc,
  find **Windows Explorer** under Processes, and choose **Restart**. Auto-hide is under Settings >
  Personalization > Taskbar > Taskbar behaviors.
- "Hide on" below means: start `Harbor.Shell.exe` from the Release output, wait for "Dock running",
  then turn on **Hide the Windows taskbar**.

## 1. Run the script on this PC

`powershell -ExecutionPolicy Bypass -File scripts\safety-check.ps1 -Arch <arch> -ExerciseAutoHide`
(add `-IncludeExplorerRestart` if you don't mind File Explorer windows closing).
Paste the result table below.

## 2. Monitors (needs a second display)

| # | Steps | Expected |
|---|---|---|
| 2.1 | Taskbar on all displays, second monitor connected. Hide on. | Both taskbars hidden. |
| 2.2 | With hide on, unplug the second monitor. | Primary stays hidden; nothing flashes repeatedly. |
| 2.3 | Plug it back in. | Its taskbar appears briefly at most, then is hidden again. |
| 2.4 | Ctrl+Alt+Shift+H. | Both taskbars back at once. |
| 2.5 | Change the scale of one display (Settings > Display) with hide on. | Stays hidden, or comes back; never stuck half-way. |

## 3. Session (logoff and restart)

| # | Steps | Expected |
|---|---|---|
| 3.1 | Hide on, then sign out and sign back in. | Taskbar visible after sign-in; auto-hide as it was before Harbor. |
| 3.2 | Same with a restart instead of sign-out. | Same. |
| 3.3 | Hide on, then start a sign-out but cancel it (an app with unsaved work asks). | Taskbar hidden again once the sign-out is cancelled. |
| 3.4 | After 3.1: start Harbor Shell. | Nothing changes; if Harbor had to restore auto-hide, its window says so. |

## 4. Everyday use with hide on

Note anything that shows the taskbar again, and whether it flickers.

| # | Action | Notes |
|---|---|---|
| 4.1 | Win key, Start, Esc | |
| 4.2 | Win+T, Win+B, Win+number | |
| 4.3 | A notification arrives; open the notification center (Win+N) | |
| 4.4 | Full-screen video or game, then leave it | |
| 4.5 | Move the mouse to the bottom edge repeatedly | |
| 4.6 | Sleep the PC with hide on, wake it | Stays hidden, no restore loop |

## 5. x64 PC

Repeat sections 1 and 4 on the x64 machine (x64 build). ARM64-only differences go in the notes.

## Results

| Date | PC (arch, Windows build) | Section | Result | Notes |
|---|---|---|---|---|
| 2026-10-14 | ARM64 (Snapdragon), 26H1 build 28000.3086, one display | 1 (`-ExerciseAutoHide`) | 8/8 pass | Restores: panic 0.2 s, dock killed 0.2 s, Shell closed or killed 0.2 s, dock frozen 7.2 s, both killed then Shell started 3.6 s. Start menu: no re-show. Idle: 0.014 % CPU, 18.7 MB. Explorer shows the taskbar ~3 times right after auto-hide turns on (re-hidden). Run before the record-cleanup check was added. |
