# 10. Running windows: tracking, app identity and pinning from the dock

- Status: Accepted
- Date: 2026-10-15

## Context

M3 makes the dock show what is running and switch between windows, like the taskbar it replaces. Windows has
no API for "the taskbar's buttons": a replacement has to follow top-level windows itself, decide which ones a
person would call an app window, group them by app, and name and draw those apps. The dock must stay lean
(rule 4: no shell COM in the dock, where icons alone cost 33 MB in M2), keep answering pings (rule 1: a hung
app must never stall the UI thread), and never log which apps a person runs.

The spike (M3 step 0, Windows 11 26H1, ARM64, two monitors; plus both CI runners) measured:
- **Identity.** Chrome sets an AppUserModelID on its windows; packaged apps (WhatsApp, a Store game) have one
  only through their package; VS Code has neither; Settings is an `ApplicationFrameWindow` whose window has
  one. Packaged apps' frames that aren't showing are shell-cloaked on the current desktop.
- **Volume.** Three minutes of ordinary use with a browser scrolling: 12 foreground changes, 21 shows, 20
  hides, 47 destroys, 8 cloak changes. Scrolling adds none. 0.15 % of one core for the hooks.
- **Elevated windows.** `ShowWindowAsync(SW_MINIMIZE)` fails on an elevated window (UIPI); a posted
  `WM_SYSCOMMAND`/`SC_MINIMIZE` works.

## Decision

**A window watcher thread in the dock** (`ports/window_system.h`, `adapters/win32_window_system`,
`app/window_watcher`), with its own message loop. It hands snapshots to the UI thread through a latest-value
mailbox, so the UI thread never waits on it.
- WinEvent hooks, out of context, skipping our own process: foreground, minimize start/end, object
  destroy/show/hide, cloaked/uncloaked, move-size start/end. Callbacks check `OBJID_WINDOW` and only queue;
  the queue is drained after the pump returns, because callbacks re-enter while COM calls pump.
- `EnumWindows` at start and after `TaskbarCreated`; nothing polls.

**Which windows count** (a pure function of the window's facts, `core/windows`): the Alt+Tab rules. Visible;
unowned or `WS_EX_APPWINDOW`; not a tool window; not the shell window, Progman/WorkerW, Explorer's tray
windows, `Ghost` windows (the hung original stays) or Harbor's own. Windows on another virtual desktop
(`IVirtualDesktopManager`) and shell-cloaked windows are left out; a window its app cloaked itself stays.

**An app's identity, in order:**
1. the window's AppUserModelID (`SHGetPropertyStoreForWindow`, `PKEY_AppUserModel_ID`);
2. for an `ApplicationFrameWindow`, the hosted CoreWindow's process AppUserModelID;
3. the process's package AppUserModelID;
4. the process's image path (`QueryFullProcessImageName`).

Process facts are cached by (process id, start time). Identity is read again 0.5 s and 2 s after a window
shows and when it comes to the front, since Chromium and Electron apps can set theirs late: a window may move
to another app.

**Order.** Apps that aren't pinned follow the pinned ones, after a divider, in the order their processes
started. Each app's windows keep two orders: most recent first (from foreground events, seeded from the
z-order) and the order they appeared, which clicking cycles through.

**Clicks** (`core/island/click_policy`, pure). The foreground window is read at click time and mapped to its
root owner.
- The app isn't in front: its most recent window comes forward.
- The app is in front: its next window comes forward, or, if it has only one, it is minimized.
- The app isn't running: it starts, as in M2.

Bringing a window forward: `AllowSetForegroundWindow` for its process, a posted `WM_SYSCOMMAND`/`SC_RESTORE`
if it is minimized, then `SetForegroundWindow`. Minimizing: a posted `SC_MINIMIZE`. Posting works for
elevated windows, where `ShowWindowAsync` doesn't.

**The dock never blocks on another app.** No `SendMessage` to another process's window: `PostMessage`
(`WM_CLOSE`, `WM_SYSCOMMAND`), `GetWindowText` (which doesn't wait on the other app) and shell property reads
only. A test keeps a menu open for more than 10 s and checks that pongs keep coming.

**Names and icons come from Harbor Shell**, as for pins (ADR 0009), over three new messages
(contracts/README.md):
- `appLookup` v1 (dock → Shell), once per newly seen app: `{id, aumid|null, exe|null, hwnd|null}`. `id` is a
  number the dock never reuses; an AppUserModelID over 130 characters or a path over 2048 isn't looked up.
  The window lets the Shell read its relaunch name and icon (browser profiles, web apps). At most 64 are in
  flight; all live ones are sent again after a reconnect.
- `appInfo` v1 (Shell → dock): `{id, name, icon|null, pinned|null, launch|null}`. The Shell may answer twice:
  quickly from what it knows, then with the real name and icon. The dock applies `dockItems` and `appInfo`
  in one ordered queue. Apps the Shell hasn't answered for stay off the dock for up to 250 ms, then appear
  under their file name with a placeholder icon.
- `pinRequest` v1 (dock → Shell): `{action: pin|unpin, kind, target}`. The dock changes nothing until the
  Shell's next `dockItems`.

A pinned app and a running one are the same item when the pin's target is the running app's
AppUserModelID or the path its window came from; the Shell computes each pin's keys (target path, a
shortcut's AppUserModelID) once per settings change. So a pinned app the dock started never shows up as
unpinned first.

**The Shell's icon files.** `IconStore` keeps every icon an `appInfo` on the current connection names, not
only the pinned ones.

**Pinning writes settings.json** (amends ADR 0009, where the Shell never wrote an existing file):
- atomically: a temporary file, then a rename;
- only if the file is unchanged since the Shell last read it (same hash) and decodes; otherwise the request
  is refused, logged, and shown in Harbor Shell;
- without duplicates: pinning something already pinned changes nothing.

**Menus and the name label.**
- The right-click menu (the app's name, its windows by title, New window, Pin to dock / Unpin from dock,
  Close window / Close all windows) and the overflow "…" menu (the apps that didn't fit, with their icons) are
  Win32 popup menus (`TrackPopupMenuEx`): keyboard, Narrator (`MSAAMENUINFO`) and dismissal come with them.
  They are owner-drawn in the island's colors, flat (no submenus), and plain system menus in high contrast.
  Their bitmaps are freed when they close.
- A menu needs its owner in front: the island takes the foreground, opens the menu, posts `WM_NULL` after
  it, and gives the foreground back to the window that had it.
- The name label is a small topmost popup that never activates and lets clicks through
  (`WS_EX_LAYERED | WS_EX_TRANSPARENT`, composition-drawn; spike-checked). Its text renderer is created on
  the first hover. The spike's first DirectWrite draw cost 6 MB, so DirectWrite and GDI text are measured
  against each other before one is chosen; the result is added here.

**Tests never see the user's windows.** In test mode, `--test-windows <id>` limits the watcher to windows of
the `harbor_test_windows` helper, which gives each window its own AppUserModelID
(`Harbor.Test.<id>.<n>`) and shows, hides, minimizes, cloaks, owns, hangs or closes it on request.

**Privacy.** Both processes log app ids and counts, never window titles, AppUserModelIDs or paths: they name
apps and the user's account folder.

## Alternatives considered

- **The shell hook's window messages** (`HSHELL_WINDOWCREATED`/`DESTROYED`/`ACTIVATED`) as the source:
  documented but "not intended for general use", and blind to cloaking and ownership changes. The dock uses
  the shell hook only for attention (ADR 0011).
- **Polling `EnumWindows`**: idle CPU for what the hooks report for free.
- **Reading the real taskbar's buttons through UI Automation**: scraping (rule 6), and the taskbar is hidden.
- **Names and icons in the dock**: the shell COM cost of M2's spike, again.
- **Showing pins as changed before the Shell confirms**: two sources of truth for the pinned list.

## Consequences

- Apps without an AppUserModelID group by executable: two apps run by one host (`java.exe`, `python.exe`)
  share an item.
- An app that sets its AppUserModelID late can appear under its executable for up to 2 s, then move.
- Only the current virtual desktop's windows show; a click on an app whose windows are all elsewhere starts it.
- The watcher's idle cost is the hooks' (spike: 0.15 % of one core with ordinary use, nothing at rest).
- The arm64 CI runner starts with Windows' first-sign-in screen above every window, where no other app can
  take the foreground. CI closes it (`WWAHost.exe`, then Esc for Start) before the end-to-end tests that need
  the foreground; those tests check it and fail with a clear message otherwise. The x64 runner needs nothing.
