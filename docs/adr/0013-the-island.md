# 13. The island: a glass notch at the top, hosted by Harbor Shell

- Status: Accepted
- Date: 2026-10-16

## Context

Harbor's second surface (after the dock) is "the island". It is a small glass notch at the top centre of the
screen, like a Dynamic Island. It grows when the mouse comes near and opens on a click into sections side by
side: Controls, Today, Stats, Clipboard, Apps (search), Wallpaper and Power, plus what's playing and
notifications. The dock's Start button and the Windows key open it on Apps (ADR 0015). M4 delivers the island
with Apps + search, Stats, Power, Media and Controls (ADR 0016); the other sections come later.

The island is rich UI: a search box, lists, sliders, media controls, all reachable by screen readers. The user
chose to run it inside Harbor Shell, which is already a WinUI 3 app running all the time. The dock stays the
lean C++ process (rule 4).

The M4 spike (2026-10-16, ARM64, 200 % and 100 % monitors) tried three designs:
1. **A WinUI 3 `Window`**, borderless and topmost, with a transparent system backdrop. The corners weren't
   transparent and Windows still drew a frame or shadow. The user found it wrong.
2. **The same window with Windows.UI.Composition glass behind the WinUI content.** The glass never showed.
   Animating `RectangleClip` corner radii throws `E_INVALIDARG`, which crashed the window as a stowed
   exception from a XAML event handler.
3. **A plain Win32 popup.** It had `WS_EX_NOREDIRECTIONBITMAP`, tool window, topmost and no-activate.
   - Its glass came from Windows.UI.Composition: host backdrop brush plus tint, clipped by a `RectangleClip`
     with a flat top and rounded bottom corners. This is the dock island's technique (ADR 0008).
   - Its controls came from a WinUI XAML island (`DesktopWindowXamlSource`) inside the popup.
   - The XAML content draws transparently over the glass: clean anti-aliased corners, no frame, real blur.
   - Compositor animations ran: bubble ~207 ms, expand ~251 ms, collapse ~189 ms.

   The user chose the look: a **notch on the top edge, empty when collapsed, glass**.

## Decision

**The window.**
- Harbor Shell owns a plain Win32 popup, as in spike design 3.
- **States** (pure `IslandModel`):
  - Hidden;
  - Collapsed: an empty notch about 200×30 DIP;
  - Bubble: about 320×44 DIP;
  - Expanded: the panel, about 980×420 DIP; narrower monitors (1024×768, 1280×800 DIP) get a rail or
    scrolling.
- **Placement:** the top centre of the **dock's monitor**, which the dock reports in `dockState`. The shape is
  flush with the top edge.
- **Animations:** they run in the compositor, on offset, size and clip edges. The window grows to the target
  size first and shrinks after the shape has. Clip radii are set, not animated.

**Interaction**, with no cursor polling and no mouse hook:
- A thin, nearly transparent trigger strip at the top centre makes the notch grow into the bubble. From then
  on, the window's own pointer events track the mouse; leaving it shrinks the bubble back.
- A click, or the island's private hotkey (ADR 0015), expands it. The Shell receives that hotkey, so it may
  take the foreground; it does, and puts the focus in the search box on Apps.
- Esc, losing the focus, or launching something collapses it.
- `islandState` tells the dock whether the island is ready and whether it is open.

**When it hides.**
- A full-screen app on its monitor, or the dock hidden by the panic hotkey (`dockState.fullScreen`,
  `dockState.visible`), hide the island.
- **It never takes part in rule 1.** The taskbar safety rules are about the dock alone (ADR 0007). A missing,
  hung or hidden island never shows or hides the taskbar.

**Glass, when available.**
- Glass is used when transparency effects are on and **energy saver is off**.
- Otherwise the notch is a solid tint, as are Windows' own flyouts. With energy saver on, the host backdrop
  draws black.
- The spike found that energy saver is reported by the power setting `GUID_ENERGY_SAVER_STATUS`
  (Windows 11 24H2+; `RegisterPowerSettingNotification`): 1 with it on, 0 off. `SystemStatusFlag`,
  `PowerManager.EnergySaverStatus`, `UISettings.AdvancedEffectsEnabled` and the transparency setting stayed
  the same. The effective power mode also changed (`BetterBattery`), but it does that for "best power
  efficiency" as well.
- The status is followed live, and the old battery-saver flag still counts. The dock gets the same rule
  (amends ADR 0008).

**The code.**
- `src/Harbor.Platform` (`net10.0-windows10.0.26100.0`) holds the Win32, WinRT and COM adapters: the island
  window, composition, the system data sources of ADR 0016.
- `Harbor.Core` keeps the ports and the pure logic: model, placement, search.
- Harbor Shell gets a composition root (`ShellHost`) and a **test mode**: pipe name, data dir and test
  taskbar are passed through to a test dock, and every system-changing source is replaced by a fake.
- Closing the main window exits the Shell, so no island is left running without its watchdog.

**Privacy and budget.**
- What's playing, search text and result names are never logged; counts and timings only.
- Collapsed: no timers, about 0 % CPU, and the island adds at most 25 MB private to the Shell. The spike
  measured +1 MB.
- Expanded: at most +60 MB, released within 30 s of collapsing; Apps stays warm.

## Alternatives considered

- **A WinUI 3 `Window`**: no shaped, per-pixel transparent glass (spike designs 1 and 2).
- **A window region for the shape**: hard, aliased edges, and a hard edge on the blur.
- **The island drawn by the dock** (C++ composition), with the panel content elsewhere: leaner, but every
  control (text input, lists, accessibility) would be hand-made, and the user chose Harbor Shell.
- **Its own process**: isolated from the watchdog, but roughly 40–60 MB more for a second WinUI runtime.

## Consequences

- Harbor Shell is now a UI process users touch every day, not only a watchdog with a status window. A crash in
  the island ends the Shell; the dock then restores the taskbar as for any lost Shell (ADR 0007), and the Shell
  is started again by the user (autostart, M5).
- Two composition stacks live in the Shell: Windows.UI.Composition for the glass, WinUI (lifted) for the
  controls.
- The notch covers the top centre of maximized windows' title bars (tabs, VS Code's command centre). It is
  small and empty when collapsed.
