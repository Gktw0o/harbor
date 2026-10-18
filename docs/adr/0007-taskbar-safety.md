# 7. Keeping the user from ever losing the taskbar

- Status: Accepted
- Date: 2026-10-14

## Context

CLAUDE.md rule 1: whatever happens to Harbor, the user keeps a working taskbar. The taskbar must come back
on a normal exit, a crash, sign-out or shutdown, a hang, and on demand (a panic hotkey).

What Windows offers is limited:
- An unhandled-exception filter misses most fatal failures. It doesn't see fail-fast exits (Control Flow
  Guard and /GS violations, heap corruption), release-mode `abort` and `std::terminate`, or the default
  invalid-parameter handler. It never runs under a debugger, and stack overflows are unreliable.
- Windows Error Reporting's recovery callback needs a user interacting with the app to count a hang, and it
  is unclear whether it runs for fail-fast exits.
- `WM_ENDSESSION` gives about 5 s, and the work must happen inside the handler.
- A kill-on-close job ends its processes without warning.

## Decision

**The guarantee is a second process.** Harbor Shell runs a watchdog (`DockSupervisor`); the dock's own
handlers are best effort. Each one covers what the other can't see.

**The dock hides the taskbar only while all of these hold** (`GuardPolicy`, a pure state machine):
1. Harbor Shell asked, on the current connection: a dropped connection forgets the request.
2. The watchdog is alive. The supervisor itself sends the pings, so a ping proves the watchdog runs, not
   just the pipe. Hiding needs 3 pings on the connection and the last one within 5 s, measured on a clock
   that stops during sleep (`QueryUnbiasedInterruptTime`).
3. The panic hotkey (Ctrl+Alt+Shift+H) is registered: no hiding without a way out.
4. No panic or Explorer-fighting block is set. Only a request to show the taskbar clears one; Harbor Shell
   sends it when it sees the block, and turns its toggle off.
5. A taskbar exists.
6. The dock isn't in a kill-on-close job. Harbor Shell starts the dock with `CREATE_BREAKAWAY_FROM_JOB`
   when its own job allows it.
7. (Added in M2, ADR 0008; widened in M3, ADR 0011; tightened in M4, ADR 0014.) The island is on screen:
   drawn, visible, not cloaked, and inside one connected monitor. Without it the user would have neither
   taskbar nor dock. Like an unproven watchdog, this is a reason not to hide, not a block the user must clear.
   From M4 the island may be tucked behind a full-screen app; a tucked island counts as on screen only while
   its monitor's edge trigger is armed (the mouse at the bottom brings it back).

The dock also never hides while it is exiting, while the session is ending, or once a crash handler ran.

**Who restores what**

| Failure | Restored by |
|---|---|
| Hiding turned off, or the dock exits normally | the dock; Harbor Shell again once it has exited |
| Harbor Shell closed, crashed, killed, or its watchdog stuck | the dock: pipe closed, or 5 s without a ping |
| A crash the dock's handlers see: access violation (any thread), `abort` and `std::terminate` (`SIGABRT`), invalid CRT parameter, pure virtual call | the dock's crash handler shows the windows it hid (no locks, no allocation, no call into Explorer), then the crash goes on; Harbor Shell restores auto-hide once the process is gone |
| Fail-fast, most stack overflows, `taskkill /F` | Harbor Shell, when the dock's process handle is signaled |
| The dock's UI thread hangs | the panic hotkey still works (it lives on the guard thread). After 5 unanswered pings, Harbor Shell tells the dock to stop hiding (its guard thread may still be fine), shows the windows, ends the dock, and restores auto-hide |
| A debugger stops the dock | Harbor Shell shows the taskbar and turns hiding off, but leaves the process alone |
| Sign-out or shutdown | the dock, inside `WM_QUERYENDSESSION`/`WM_ENDSESSION`, notified early (`SetProcessShutdownParameters(0x3FF)`); the restore record stays for the next start |
| Explorer restarts or keeps showing the taskbar | the new taskbar is hidden again; after 10 re-hides in 10 s the dock gives up and restores |
| The dock and Harbor Shell die together | the windows come back with Explorer's restart or the next sign-in; auto-hide at the next Harbor start |

**Only the dock touches auto-hide while it is alive.** While a dock process lives, Harbor Shell only shows
windows and sends `setTaskbar {hidden:false}`. It restores auto-hide from the record only after the
process has ended, so a hung dock can't race it over the setting. A test makes the watchdog violate this
and fails.

**Threads in the dock.** The UI thread renders and answers pings, so a pong proves the UI is alive. The pipe
thread passes link changes, pings and requests **straight to the guard thread**. The guard thread owns the
panic hotkey, the WinEvent hook and the policy, and never blocks: calls that can wait on Explorer
(`SHAppBarMessage`) run on an explorer worker, where the newest request replaces a pending one.

**After any failure, hiding stays off** until the user turns it on again. No automatic re-hide, no
automatic dock restart in M1.

**Logs.** Both processes log to `%LOCALAPPDATA%\Harbor\logs` (`dock.log`, `shell.log`): one UTF-8 line per
entry with a UTC timestamp. At 1 MiB a log becomes `<name>.old.log` (one generation), so they never take
more than about 4 MiB together. Every restore says why it happened.

## Alternatives considered

- **A kill-on-close job** tying the dock to Harbor Shell: when the Shell dies, the dock would be killed with
  nothing left to restore. The dock refuses to hide in such a job.
- **WER recovery callbacks** instead of, or besides, the handlers: unreliable for exactly the failures that
  matter.
- **Only the dock restores**, with the Shell as a launcher: a fail-fast or a kill would strand the user.
- **The panic hotkey on the dock's UI thread**: a hung UI would take the way out with it.
- **Re-hiding automatically after a crash**: risks a crash-hide-crash loop; the user chose to turn it on
  again by hand.

## Consequences

- Restore times measured with `scripts\safety-check.ps1` on the real taskbar (26H1, ARM64, Release):
  - panic hotkey: about 0.2 s;
  - dock killed: about 0.2 s;
  - Harbor Shell closed or killed: about 0.2 s;
  - dock frozen (every thread): about 7 s (5 missed pings, then the kill);
  - both killed: hidden until Harbor Shell starts again, then about 3.6 s (it first waits 3 s for a running
    dock).
- Dock idle cost with the taskbar hidden: 0.014 % CPU and 18.7 MB working set (budget 0.5 % and 40 MB).
- An x64 Debug build (Debug CRT) with Control Flow Guard, running under emulation on ARM64, fails fast on a
  main-thread exception before any filter runs. Native x64 (CI), Release builds and arm64 are fine; the
  one test it affects skips itself only in that combination.
- Test runners such as ctest run tests in kill-on-close jobs, so in-process guard tests replace the job
  check; a separate end-to-end test checks the real one.

## Note (M4): a starting dock's first ping

The dock answers pings on its UI thread, which is still setting up for a moment after it connects. Before a
connection's first pong the watchdog now allows `StartupAllowance` (5 s), never fewer than the usual 5 pings;
after it, 5 missed pings in a row count as hung as before. With the real watchdog's 1 s pings both are 5 pings,
so nothing changes for users. Test supervisors ping every 100 ms: there "5 pings" was 0.5 s, and a dock starting
on a busy CI runner sometimes missed it (the watchdog then gave the taskbar back and ended a healthy dock). A dock
hung from its start is still caught, after 5 s.
