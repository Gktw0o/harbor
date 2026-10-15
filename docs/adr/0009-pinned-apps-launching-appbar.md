# 9. Pinned apps, launching, the AppBar and accessibility

- Status: Accepted
- Date: 2026-10-15

## Context

M2 gives the dock its apps and its place on screen. The pinned list has to live somewhere people can edit
before there is a settings UI (M5); the dock must stay lean (rule 4), so slow or heavy shell work shouldn't
run in it; apps started from the dock should behave as if started from the Start menu; maximized windows
must stop above the dock; and the dock must work with screen readers (rule 7's spirit).

## Decision

**Settings belong to Harbor Shell.** `%LOCALAPPDATA%\Harbor\settings.json` v1 (contracts/state/settings),
`{"v":1,"pinned":[{"app":"<AppUserModelID>"} | {"path":"<absolute path>"}]}`, at most 40 pins.
- First run writes File Explorer, Microsoft Edge, Windows Terminal and Settings; apps that aren't installed
  are left off the dock (not out of the file).
- The file is edited by hand until M5, so decoding says what's wrong and where, and an existing file is
  never overwritten. A `FileSystemWatcher` (debounced) follows edits; a broken or half-saved file keeps the
  last good list (no flash of the defaults) and Harbor Shell shows the problem.

**Harbor Shell resolves apps; the dock only draws and launches.** `dockItems` v1 (Shell → dock) carries each
item's kind, target, display name and icon file name.
- Names and icons come from the shell (`SHCreateItemFromParsingName`, `IShellItemImageFactory`, icon only,
  256 px) on Harbor Shell's side: in the dock that code cost 33 MB.
- Icon files are raw premultiplied BGRA ("HIC1" header), named by their content's SHA-256, written as a
  temporary file and renamed, never rewritten. Icons neither the new list nor the previous one uses are
  deleted.
- The message is bounded so its largest form fits the 64 KiB line (40 items, 260/128 characters, no control
  characters; 64,888 bytes): .NET's built-in JSON encoders escape non-BMP characters in 12 bytes, so
  Harbor.Core writes JSON with its own encoder (`MinimalJsonEncoder`).
- The log names items by index, never by app.

**Launching through Explorer.** `shell:AppsFolder\<AUMID>` or the path, started by Explorer itself via
`IShellWindows` → the desktop's view → `IShellDispatch2::ShellExecute` (documented COM), after checking
Explorer answers within 2 s. The app isn't the dock's child (no inherited job, environment or handles) and
can take the foreground (the click allows it with `AllowSetForegroundWindow(ASFW_ANY)`). If Explorer can't
(not running, hung, or refusing, as on the ARM CI runner), the dock calls `ShellExecuteEx`. Launches run on
their own STA thread; the route is logged.

**The AppBar.** A bottom-edge strip as tall as the island plus its margins (`core/appbar/appbar_state`, pure;
`app/appbar_worker` runs it on its own thread, since `SHAppBarMessage` is a synchronous call into Explorer).
- `ABM_QUERYPOS` then our height back on its bottom, `ABM_SETPOS` only when the strip changes, repositioning
  at most every 250 ms: `ABN_POSCHANGED` comes in bursts and can't feed itself.
- `TaskbarCreated` (Explorer restarted, or the primary DPI changed) removes and registers again;
  `ABN_FULLSCREENAPP` puts the island behind a full-screen app and back on top.
- **No AppBar record.** The spike showed Explorer frees a dead window's strip at once (83 ms in the safety
  check). Harbor Shell's watchdog still sends a best-effort `ABM_REMOVE` after a dock ends.
- Test docks use an AppBar that answers like Explorer but reserves nothing: tests never change the user's
  work area.

**Recovery.** Harbor Shell starts a failed dock again (`RestartPolicy`): after a crash, a hang it ended, or a
dock that never connected (10 s), waiting 1, 2, 4 s, at most 3 times a minute. Never after exit code 0,
`AlreadyRunning`, `InvalidArguments` or `GraphicsUnavailable`, nor while Windows signs out. "Stop dock" closes
the dock (`WM_CLOSE`, a normal exit) and it stays stopped. Hiding the taskbar stays off after any end (M1).
While hiding is on, the panic hotkey also hides the island (a way out of the dock); turning hiding on again
brings it back.

**Accessibility.** UI Automation providers (`adapters/uia_island`): the island is a tool bar named "Harbor
Dock", each item a button with its app's name, bounds and an Invoke pattern that starts it like a click.
Providers are created on the first `WM_GETOBJECT` and UIA's API sets are delay-loaded, so a dock nobody
inspects never loads UI Automation. After the items change, old providers answer
`UIA_E_ELEMENTNOTAVAILABLE`.

## Consequences

- No new dependencies: COM, the shell, UI Automation and C++/WinRT come with Windows and its SDK, on both
  architectures.
- Narrator's keyboard can't reach a never-activated window; a focus hotkey comes with the settings UI (M5).
- Overflow (more pins than fit) leaves items off with a log line; an overflow menu comes with M3.
- Changing the taskbar's auto-hide in Windows while Harbor hides it isn't noticed (`ABN_STATECHANGE` can't tell
  the user's change from Harbor's); revisited with onboarding (M5).
