# 14. The dock as an overlay, and tucking it away for full-screen apps

- Status: Accepted
- Date: 2026-10-16
- Amends: ADR 0009 (no strip), ADR 0011 (strips and triggers), ADR 0007 (rule 7, stricter)

## Context

Until M3 the dock reserved an AppBar strip at the bottom of every monitor (ADR 0009, ADR 0011), so maximized
windows stopped above it. The user wants an **overlay** instead:
- windows use the whole screen;
- the dock stays visible over normal and maximized windows;
- it hides about 2–3 s after an app goes **full screen**, and the bottom edge brings it back.

Rule 1 still holds: the user must never be left with neither taskbar nor dock. GuardPolicy rule 7 hides the
taskbar only while the island is on screen (ADR 0007, ADR 0011).

The M4 spike, with two monitors:
- An AppBar registered with `ABM_NEW` alone, never `ABM_SETPOS`, or with a zero-height `SETPOS`, still gets
  `ABN_FULLSCREENAPP` with "open" on each monitor, and no work area changes.
- **"closed" is unreliable**: on the primary monitor it never came when our full-screen window closed.
- `SHQueryUserNotificationState` said `QUNS_BUSY` the whole time, so it tells nothing.

## Decision

**No strip.**
- The AppBars stay registered, one per monitor host, only to hear `ABN_FULLSCREENAPP`. They never call
  `ABM_SETPOS`, and `core/appbar/appbar_state` gets a notify-only mode.
- The island sits at the bottom of its monitor's work area: a "virtual strip" passed to `island::Arrange`.
  With hiding on, Harbor's own auto-hide setting keeps Explorer's taskbar out of the work area.
- Harbor Shell's watchdog still sends `ABM_REMOVE` for a dead dock's hosts, which is harmless.

**Edge triggers on every monitor.** The host windows keep their alpha-1 trigger band at the bottom edge
(ADR 0011), on the other monitors as before, and on the dock's own monitor while it is tucked.

**The tuck**, a pure `core/island/tuck` state machine:
- `ABN_FULLSCREENAPP` "open" at the dock's monitor → after 2.5 s the island goes `HWND_BOTTOM`. The window
  stays shown. Menus and the label close.
- Resting on the trigger → the island comes back on top over the full-screen app; leaving it → 1 s → tucked
  again.
- On every foreground change (the window watcher already hooks it), the dock checks whether the foreground
  window still covers its monitor. If not, the full screen has ended: the island comes back on top whatever
  Explorer said. This covers the unreliable "closed".

**Rule 7, stricter.** `core/island/on_screen` gets two facts: `tucked` and `triggerArmed`.
- A tucked island counts as on screen **only while its own monitor's trigger is armed**: the window exists,
  is shown, sits at that edge, and the triggers are on.
- A broken trigger, or triggers off after the panic hotkey, make it not on screen, so the taskbar comes back.
- The worst case for a user is then the same as Windows' own auto-hide: move the mouse to the bottom.
- A full-screen app that ended unnoticed can't keep the island buried: the foreground check brings it back,
  and while it is back on top the normal rule 7 applies.

**Restore-path tests come first**, each while tucked:
- the panic hotkey;
- Harbor Shell gone;
- the dock killed or hung;
- no armed trigger;
- full screen ending unannounced.

They use new test hooks (`fullscreen-on`, `fullscreen-off`, `break-trigger`), because test AppBars can't
receive `ABN_FULLSCREENAPP`.

## Alternatives considered

- **Hiding the island** (`SW_HIDE`) for full screen: the island would be off screen, so rule 7 would restore
  the taskbar, turn auto-hide off and reflow every window.
- **Keeping the strip**: what the user didn't want; full-screen apps already ignore it.
- **Intellihide** (hide whenever a window overlaps the dock): the user chose "only for full screen".
- **Detecting full screen by polling** the foreground window: a timer at idle (ADR 0011).

## Consequences

- Maximized windows now reach the bottom edge: the dock covers their last rows (status bars, a terminal's
  prompt). The user accepted this; hiding over maximized windows could become an option later.
- Moving the dock between monitors still changes no work area.
- The safety check's AppBar scenarios become "the work area never shrinks".
