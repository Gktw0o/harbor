# 6. How the dock hides the taskbar, and how it is put back

- Status: Accepted
- Date: 2026-10-14

## Context

Harbor replaces the Windows taskbar, so it has to hide it, and CLAUDE.md rule 1 says the user must never be
left without one. Windows has no API for "replace the taskbar". The tools that do this (ManagedShell for
RetroBar and Cairo, Seelen UI, thide, Buttery Taskbar) all do the same two things:

- **turn on the taskbar's auto-hide setting** (`SHAppBarMessage(ABM_SETSTATE, ABS_AUTOHIDE)`), so Explorer
  gives the screen space back to other windows;
- **hide the taskbar windows** (`Shell_TrayWnd` and every `Shell_SecondaryTrayWnd`).

Hiding the windows alone leaves a strip where the taskbar was: maximized windows don't grow into it.

Auto-hide is a saved, global setting: Explorer writes it to the registry when the user signs out. Seelen UI
left it switched on after crashes, and restored a hard-coded value over users' own preference. Explorer also
shows the taskbar again on some events and recreates it when it restarts. And `ABM_SETSTATE` is a
synchronous call into Explorer, which can block, and which has been seen to revert itself.

## Decision

**Which windows.** `Shell_TrayWnd` and every `Shell_SecondaryTrayWnd` owned by the process that owns
`GetShellWindow()` (Explorer). Other apps create windows with those class names (tray widgets); they are
left alone. Tests never create windows with Explorer's class names (that has broken Explorer for another
project): they use `Harbor.Test.<id>.*` stand-ins and an auto-hide "setting" kept in a file
(`TaskbarTarget::Test`). The dock accepts the test options only together (`--pipe-name`, `--data-dir`,
`--test-taskbar`), so no test can point it at the real taskbar.

**Hiding** (only when the safety rules of ADR 0007 hold), on the dock's explorer worker:
1. If auto-hide is off: write the **restore record** `%LOCALAPPDATA%\Harbor\taskbar-restore.json`
   (`{"v":1,"stateBefore":0,"createdUtc":"…"}`, schema in `contracts/state/`), atomically (temp file, then
   `MoveFileEx` with write-through). Then turn auto-hide on, and check that it took.
2. Hide the windows with `ShowWindowAsync(SW_HIDE)`: it never blocks, even on a hung Explorer.
3. A WinEvent hook (`EVENT_OBJECT_SHOW`, Explorer's process only) hides a taskbar Explorer shows again.
   More than 10 of those in 10 s trips a circuit breaker: the dock gives up and restores
   (`explorerFighting`). `TaskbarCreated` (Explorer restarted) and display changes hide the new windows.

**Restoring** works the same in the dock (`Win32Taskbar::Restore`) and in Harbor Shell (`TaskbarRestorer`),
and needs nothing from whoever hid the taskbar:
1. `ShowWindowAsync(SW_SHOWNA)` on the primary taskbar and every secondary one on a connected monitor,
   **even if it looks visible**: a hide may still be queued for it, and the show must land after it.
2. If the record exists: set auto-hide back to `stateBefore` (off if the record is damaged: records are
   only written when auto-hide was off), check it again a second later, and only then delete the record.
   An unverified restore keeps the record and is retried twice, a second apart, then at the next start.
3. While the session is ending, the record is never deleted: Explorer may save auto-hide before the
   restore lands, so the next start applies the record again.
4. Both processes restore at startup. Only the dock touches auto-hide while it runs (ADR 0007).

## Alternatives considered

- **Hiding the windows only.** Nothing saved to undo, but a strip of dead screen; rejected after asking the
  user. Work-area tricks (`SPI_SETWORKAREA`) are overwritten by Explorer.
- **Restoring a fixed auto-hide value** (Seelen UI): breaks the setting for users who had auto-hide on.
- **Polling the taskbar's visibility** (ManagedShell, thide: every 100 ms): costs idle CPU; the WinEvent
  hook reports the same changes for free.

## Consequences

- Measured on Windows 11 26H1 (build 28000, ARM64) with `scripts\safety-check.ps1`: `ABM_SETSTATE` works
  there; Explorer shows the taskbar about three times in the ~150 ms after auto-hide turns on (each one
  re-hidden); Start did not show it again.
- If Harbor turned auto-hide on, crashed and is never started again, auto-hide stays on. The taskbar is
  still reachable at the screen edge; the README says how to turn the setting off.
- If the user changes auto-hide in Settings while Harbor hides the taskbar, the record still holds the old
  value. Logged; revisited with `ABN_STATECHANGE` when the dock becomes an AppBar (M2).
- M7 (MSIX) virtualizes `%LOCALAPPDATA%` for packaged apps; the dock and Harbor Shell share one package, so
  they still share the record.
