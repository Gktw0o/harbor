# 8. Drawing the dock's island

- Status: Accepted
- Date: 2026-10-15

## Context

The dock is a rounded, translucent "island" at the bottom of the screen that must follow the Windows
appearance settings (CLAUDE.md rule 7), stay lean (rule 4: idle < 0.5 % CPU, working set < 40 MB, no .NET
or WebView in the dock), and later animate (M7). It must also never be the reason the user is left without a
taskbar (rule 1).

The options for a blurred, rounded surface in a plain Win32 process:
- DWM's own system backdrop (`DWMWA_SYSTEMBACKDROP_TYPE`): corner radius fixed at 8 px, and likely solid on
  a window that is never activated.
- DirectComposition: no blur of what is behind the window.
- The Windows App SDK's `DesktopAcrylicController`: a 60 MB runtime.
- `SetWindowCompositionAttribute`: undocumented.
- **Windows.UI.Composition** from C++/WinRT: the host backdrop brush (Windows' own blur of what is behind),
  any clip shape, animations off the UI thread. The SDK ships the headers and `cppwinrt.exe`: no new
  dependency, on x64 and ARM64 alike.

## Decision

**Windows.UI.Composition, from the SDK's C++/WinRT** (`adapters/composition_renderer`).
- The window is a `WS_POPUP` with `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST |
  WS_EX_NOREDIRECTIONBITMAP`, `DWMWA_USE_HOSTBACKDROPBRUSH` and its own rounded region (so its corners don't
  take clicks). It answers `WM_MOUSEACTIVATE` with `MA_NOACTIVATE`, and is shown only after its first frame
  is committed (`RequestCommitAsync`).
- The visual tree: the host backdrop brush, a tint, item surfaces, a hover highlight and a hairline, clipped
  by a `RectangleClip` with 18 DIP corners. Icons are Direct2D-drawn into composition surfaces on a **WARP**
  device: a few static surfaces don't need the GPU driver, and WARP measured smaller (spike: 25 MB with WARP,
  32 MB with hardware).
- **Appearance** (`core/island/appearance`, pure): dark or light mode, transparency, energy saver and high
  contrast choose the style. The raw backdrop brush draws black without transparency, so the tint becomes
  opaque then (and on energy saver); high contrast uses the theme's colors. The window re-reads the
  settings on `WM_SETTINGCHANGE`, theme, color, DPI, display and power-setting changes.
- **Layout** (`core/island/layout`, pure): 48 DIP icons, 8 padding, 6 gap, 8 margin, scaled by the primary
  monitor's DPI; icons shrink to 24 DIP on a narrow screen, then items are left off; an empty dock is one empty
  slot wide. Every point of the island belongs to the nearest item (no dead zones). `WM_DPICHANGED`'s
  suggested rectangle is ignored: the layout follows the primary monitor wherever the window is (from M3,
  the dock's monitor: ADR 0011).
- **Icons are files, not shell code in the dock.** Extracting icons in-process cost 33 MB (spike). Harbor
  Shell extracts them and writes raw premultiplied BGRA files (contracts/README.md); the dock reads them on
  each redraw instead of keeping 10 MB of decoded pixels.
- **Rule 1:** the taskbar may be hidden only while the island is on screen (`GuardPolicy` rule 7, see
  ADR 0007): committed, visible, on the primary monitor (from M3: not cloaked, inside one connected monitor,
  ADR 0011). Not presentable is a reason not to hide, not a block
  the user must clear, so the IPC contract didn't change. A lost graphics device takes the island off
  screen (the taskbar comes back), gets a new device and redraws; after 5 failures 2 s apart the dock exits
  with `GraphicsUnavailable` (0x20000003), which Harbor Shell doesn't restart. `DisableProcessWindowsGhosting`
  keeps a hung UI thread from turning the topmost island into a framed "Not Responding" ghost.
- The UI thread is a COM STA with the compositor's dispatcher queue; exit order is the guard (taskbar back)
  first, then the AppBar, the window, and composition last.

## Consequences

- Working set with the island drawn: 34.2 MB (safety check, ARM64, 200 %, with UI Automation loaded by the
  script's own UIA queries); 0.006 % CPU idle. Under rule 4, with less headroom than M1's 18.7 MB: the next
  milestones must measure.
- M3 measured (safety check `budget`, ARM64, two monitors). Icons are scaled down on the CPU before Direct2D
  sees them: WARP kept about 1.5 MB for every 256 px bitmap it scaled, 80 MB private memory with 30 more
  apps. After the fix, the idle dock on the user's desktop is at 31.5 MB working set and 8.4 MB private.
  With 30 more apps it is at 41.1 MB working set and 10.5 MB private; right after a label and a menu it
  briefly reaches 46.5 MB. The user decided (2026-10-16) how rule 4's 40 MB applies: it holds for the idle
  dock's working set, and with many apps the measure is private memory, which must stay small and flat. What
  is above private memory is shared system code (composition, Direct2D/WARP, menus) that Windows can trim.
  The dock never trims its own working set. M7's performance check looks at it again.
- (Explained in M4, ADR 0013/0016: Windows 11's energy saver turns the host backdrop black, and only
  `GUID_ENERGY_SAVER_STATUS` shows it, so the dock now also chooses its solid tint under energy saver.)
  The host backdrop isn't guaranteed: on the development PC it drew black one night (on battery, transparency
  on, energy saver off) in the dock and in the spike alike, while the spike showed real blur earlier the same
  day. The island then looks like dark glass (tint over black). The cause is open (M2 checklist 2.1); light
  mode over black is a duller grey.
- C++/WinRT exceptions are caught at the adapter's boundary; the window never sees them.
- Animations (M7) can run on the compositor without the UI thread.
