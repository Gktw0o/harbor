# 11. One dock across monitors, the "on screen" rule, and attention marks

- Status: Accepted
- Date: 2026-10-15

## Context

The user chose one dock that follows the mouse: it starts on the primary monitor and moves to another monitor
when the cursor rests at that monitor's bottom edge, the way macOS's Dock does. Maximized windows must not
reflow when it moves. Rule 1 ties the hidden taskbar to the island being on screen (ADR 0007, rule 7), which
M2 defined as "on the primary monitor"; a moving island must never break that rule, even for a moment, or the
guard would show the taskbar and turn auto-hide off, reflowing every window.

With the taskbar hidden, the user also loses its flashing buttons: an app that wants attention
(`FlashWindowEx`, a prompt behind another window) has nowhere to say so.

The spike (M3 step 0) measured on two monitors (ARM64, 192 and 96 dpi) and on both CI runners:
- AppBar strips registered on both monitors at once work; each monitor's work area shrinks and comes back.
- About 25 `ABN_POSCHANGED` arrive right after registering; `ABN_FULLSCREENAPP` arrives per monitor, at that
  monitor's AppBar window.
- With the user's own taskbar auto-hide on, Explorer's hidden taskbar keeps the monitor's bottom 2 px, so a
  1 px window at the very edge never sees the cursor there. With the taskbar hidden by Harbor, it does.
- `HSHELL_FLASH` (0x8006) reaches a shell hook window for `FlashWindowEx`. Bringing a minimized window
  forward with `SetForegroundWindow` raised an `HSHELL_FLASH` for that window too, although it came forward.

## Decision

**A strip on every monitor; only the island moves.**
- Every monitor gets a host window (`Harbor.Dock.Host`) that never activates and holds that monitor's AppBar
  strip, each with its own `AppBarWorker` thread; hosts come and go with the monitors. The strips stay while
  the island is elsewhere, so moving the dock never changes a work area. Each strip is as tall as the island
  would be on that monitor. Harbor Shell's watchdog releases every host's strip after a dock ends.
- Each other monitor's host is also its edge trigger: nearly transparent (alpha 1, so it still takes the
  cursor), from a row 2 px above the bottom of the strip (above Explorer's auto-hidden taskbar, when there is
  one) down to the monitor's edge, so a cursor that jumps past the row still lands on it. When the cursor
  reaches it and is still at or below that row on that monitor 400 ms later, the island moves there. Not
  while a mouse button is down, a window is being moved or resized, a menu is open, or a full-screen app is in
  front on that monitor (the trigger hides there). A monitor whose bottom edge touches another monitor has no
  trigger: the cursor can't rest there.
- The island moves with **one** `SetWindowPos` (position and size; no hide or show in between), after laying
  itself out at the target monitor's DPI. A move never shows an island the panic hotkey hid.
- Monitor geometry (stacked monitors, negative coordinates, mixed DPI) and the trigger areas are pure code
  (`core/island/monitors`).
- When the dock's monitor goes away, the island goes back to the primary monitor.

**Rule 7 becomes "the island is on screen"** (amends ADR 0007 and 0008): its first frame is committed, it is
visible, it is **not cloaked**, and it lies entirely inside one connected monitor (`MonitorFromRect` with
`MONITOR_DEFAULTTONULL`). The UI thread reports the rule after it has handled a display change, so moving the
island back from a monitor that went away doesn't show the taskbar on the way.

**The panic hotkey** also tells the UI thread to close any open dock menu, hide the name label and turn the
edge triggers off, so nothing of Harbor's is left in the way.

**Layout follows the dock's monitor** (amends ADR 0008, where it followed the primary): sizes in DIPs times
that monitor's DPI. `WM_DPICHANGED`'s suggested rectangle is still ignored.

**Full screen.** `ABN_FULLSCREENAPP` at the dock's monitor puts the island behind the app, as in M2; at
another monitor, it only hides that monitor's trigger.

**Attention marks.** The window watcher's thread registers a shell hook window
(`RegisterShellHookWindow`) and listens for `HSHELL_FLASH`: the window's app gets a mark on the dock, cleared
when one of its windows comes forward or the window that flashed goes. A flash for the foreground window, or
for a window the dock brought forward in the last second, is ignored (the spike's restore case). The window
is mapped to its root owner first, as for the foreground.

**Hung windows.** When a window stops answering, Windows hides it behind a `Ghost` window. Ghosts are left
out (ADR 0010), and a known window that turns invisible stays while `IsHungAppWindow` says it is hung, so
the app keeps its item. (`HSHELL_WINDOWREPLACED`, considered in the plan, isn't needed for that.)

UI Automation reports the mark in the item's `ItemStatus` ("needs attention"). The log says which app id asks,
never which app.

## Alternatives considered

- **A dock on every monitor**: more windows, more memory, and items duplicated; the user chose one dock.
- **Moving the strip with the island**: maximized windows would reflow on every move.
- **A hook or a timer watching the cursor** for the edge: a low-level mouse hook runs code on every mouse
  move system-wide; a timer breaks "no timers at idle". The host windows get the cursor for free.
- **Hiding the island while it moves**: one frame without an island is "not on screen".
- **No attention marks**: with the taskbar hidden, prompts would go unseen. The shell hook is documented but
  "not intended for general use"; if it stops delivering, only the marks are lost.

## Consequences

- The strip costs every monitor the same height, also those without the island.
- A monitor that sits directly above another can't call the dock by mouse.
- Multi-monitor behaviour is checked by pure tests and the user's checklist: the CI runners have one display.
- Whether a virtual-desktop switch cloaks the island wasn't seen in the spike (tool windows normally show on
  every desktop); the M3 real checks confirm it, since a cloaked island now shows the taskbar.
