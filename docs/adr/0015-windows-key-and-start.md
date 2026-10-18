# 15. The Windows key and the Start button open the island

- Status: Accepted; in M4 only the Start button was built (see the note at the end)
- Date: 2026-10-16
- Amends: ADR 0012 (injected input)

## Context

The user wants the dock's **Start button** and the **Windows key** to open the island on Apps + search (ADR
0013), with Windows' own Start menu still reachable from there. Taking over a lone Windows key press means
watching the keyboard with a low-level hook (`WH_KEYBOARD_LL`). Such a hook is documented but sensitive:
- every key press on the machine passes through it;
- Windows silently removes a hook that is too slow;
- a mistake can break typing or leave a key stuck;
- the panic hotkey (rule 1) must keep working.

The M4 spike (2026-10-16, ARM64; three rounds with the user pressing the keys):
- **Inject the mask key inside the lone Win-up callback.**
  - Windows opens Start on a Win-up when no other key came since the Win-down, so Harbor injects an
    unassigned key (vkE8, AutoHotkey's mask key) into that gap.
  - Input injected with `SendInput` *inside* the callback is processed **before** the event being hooked.
    Injected inside the Win-down callback, the mask came before the Win-down, and Start still opened every
    time (round 1).
  - Posted to run after the Win-down callback, it raced: fast taps released Win before the post ran, and
    Start or quick settings leaked (round 2).
  - Injected inside the **lone Win-up** callback, it lands just before that Win-up: 207 of 207 lone taps kept
    Start closed, fast bursts included (round 3).
  - Win+E/R/D/X/V/Tab/Space/./arrows/1/Esc and Shift+Win+S were all unaffected.
- **Get the foreground through a hotkey.**
  - `AllowSetForegroundWindow` from the hook's process does nothing: a hook doesn't make it the process that
    received the last input.
  - A **private hotkey** works: the island's process registers F22 with `RegisterHotKey`, the hook's process
    injects F22 after the lone Win-up, and the `WM_HOTKEY` lets the island call `SetForegroundWindow`. The
    island was in front every time.
- **Keep the callback short.** It took a median of 0.1–0.4 µs. The slow cases (p99 several ms) came from
  checking the foreground process's elevation inside it.

## Decision

**The Start button** is the dock's first item: a glyph drawn like the tray chevron, UI Automation button
"Start". A new `core/island/slot_map` places it with the slots, "…" and the tray button. Clicking it does what
a lone Windows key does.
- (Step 5.) Until the island exists, and as the button's fallback after it, the button opens Windows' Start
  with `WM_SYSCOMMAND`/`SC_TASKLIST`, which Windows documents as "activates the Start menu". No keys are
  injected: a held Shift can't turn Ctrl+Esc into Ctrl+Shift+Esc (Task Manager), and an elevated window in
  front can't block it. The hook thread, which has no window, keeps Ctrl+Esc.

**The hook** lives in the dock (C++), on its own `app/keyboard_hook` thread. It is never in .NET, where a
garbage-collection pause would stall typing system-wide.
- **The thread** has high priority with power throttling off. It runs only the hook's message loop: no COM,
  no IPC writes.
- **It never swallows a key.**
  - A lone Win-up (no other key, no modifier, since the Win-down) gets the mask key injected inside its own
    callback; then the Win-up passes on.
  - Every other key passes untouched.
  - A removed, timed-out or crashed hook therefore falls back to Windows' own behaviour, and no key can stick.
- **It skips** (leaves the key entirely to Windows):
  - injected input (`LLKHF_INJECTED`), including its own;
  - an elevated foreground, or one it can't check: injection into it would fail;
  - remote and virtual machine windows (mstsc, vmconnect, VMware, VirtualBox).
- **The elevation check is cached** on foreground changes, not done in the callback.
- **Opening the island.** After the lone Win-up, the hook thread injects the island's hotkey.
  - Harbor Shell registers F22 (else F21 or F23) and reports which key in `islandState`.
  - The Shell's `WM_HOTKEY` gives it the foreground; it opens the island on Apps with the search box focused.
- **Fallback to Windows Start.**
  - If `islandState.open` doesn't turn true within 500 ms, the hook thread injects **Ctrl+Esc**: Windows' Start.
    Ctrl+Esc is not the Windows key, so ADR 0012's concern (a stuck Win) doesn't apply. It is one atomic
    `SendInput`.
  - After two misses in a row the hook stays off until Harbor Shell reports ready again.
- **Installed only on the Shell's go-ahead:** after `islandState{ready, hotkey}` arrives on the current
  connection.
- **Removed** by posts straight to the hook thread, so a hung UI thread can't keep it:
  - from the pipe thread when the link drops;
  - from the guard thread on the panic hotkey (it stays off until hiding is turned on again);
  - on exit.
- **Reinstalled** after unlock, resume and `TaskbarCreated`.
- **Privacy:** the hook never records keys.

**Windows' Start menu** stays one step away: an entry in the island's Apps, and the 500 ms fallback.

**Tests never take the user's Windows key.** Test docks act only on injected events carrying a test marker in
`dwExtraInfo`. The interception tests run only in CI or behind `HARBOR_FOREGROUND_TESTS`, and each ends with
Start closed.

## Alternatives considered

- **Swallowing the Win-up and replaying it**, as AutoHotkey does: a replay that fails, silently under UIPI,
  leaves Win stuck.
- **The hook in Harbor Shell (.NET)**: a garbage-collection pause stalls every keystroke on the machine.
- **`AllowSetForegroundWindow` plus an IPC request**: it doesn't work from a hook (spike).
- **A registered hotkey for the Windows key itself**: `RegisterHotKey` can't take a lone Win.
- **Replacing Start through Explorer** (shell hooks, injection): rule 6.

## Consequences

- One documented low-level hook runs in the dock while the island is reachable. A frozen dock adds key lag
  until Windows drops the hook or the watchdog ends the dock (about 7 s): accepted and documented.
- With an elevated app in front, the Windows key opens Windows' Start as usual.
- A keyboard without the Windows key can still use the dock's Start button.

## Note (end of M4, 2026-10-18)

M4 shipped the **Start button** part of this decision: it opens the island, and Windows' Start when the island
can't (Harbor Shell not ready, or no answer within 500 ms). The **Windows key** part was not built in M4, by the
user's decision: the Windows key keeps opening Windows' own Start, and the dock installs no keyboard hook. It
stays an open decision for a later milestone.
