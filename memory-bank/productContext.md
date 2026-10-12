# Product Context

## Why
The Windows 11 taskbar is rigid (fixed position, limited customization) and Windows has no
built-in, glanceable hardware monitor. Mac switchers miss the Dock and Finder.

## Users
- Power users and developers on Windows 11 desktops and laptops.
- Laptop users (incl. ARM64) who want battery/thermal awareness at a glance.

## Experience principles
- **Glanceable**: stats readable in < 1 s; detail on hover/click, not by default.
- **Calm**: no flicker, no layout jumps; values animate, they don't jump.
- **Familiar**: Dock behaviors match macOS where sensible (pinned left, running dots,
  bounce on launch, right-click menu, drag to reorder, auto-hide option).
- **Respectful**: zero admin rights needed for core features; elevated features are opt-in
  and explained.

## Core user flows (v1)
1. Install → first run explains the taskbar will be hidden + shows the panic hotkey → enable.
2. Click a pinned app → launches, icon bounces, running indicator appears.
3. Click a running app's icon → activates; click again → minimizes.
4. Hover the stats island → expands to detail (per-core CPU, per-drive storage, GPU engines).
5. Settings: position (bottom/left/right), size, magnification, auto-hide, which stats to show.
6. Quit or crash → taskbar comes back.
