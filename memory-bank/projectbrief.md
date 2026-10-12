# Project Brief

## What
A native Windows 11 shell enhancement with three parts:
1. **Dock** — a floating, macOS-style "island" dock that replaces the taskbar: pinned apps,
   running windows, launch/activate/minimize, magnification-style hover, rounded translucent surface.
2. **Stats island** — compact live readouts inside/next to the dock: CPU, RAM, storage, GPU,
   battery, temperatures (where available).
3. **Finder (v2)** — a Finder-style file manager (column view, previews, tags, fast search).

## Goals
- Feels as fluid as macOS (60+ fps animations, no jank on hover).
- Native on **Windows 11 x64 and ARM64** (Snapdragon X class laptops are first-class).
- Safe: it can never leave a user stuck without a working taskbar.
- Light: always-on processes cost almost nothing at idle.

## Non-goals (v1)
- Replacing Explorer as the shell process.
- Kernel drivers, DLL injection, or patching explorer.exe.
- Windows 10 support.
- Finder (v2 only; dock + stats ship first).

## Success criteria for v1
- Daily-drivable for a week on both an x64 desktop and an ARM64 laptop with the taskbar hidden.
- Idle: dock < 0.5 % CPU, < 40 MB; sensors service < 1 % CPU at 1 Hz sampling.
- Zero "lost taskbar" incidents across crash/kill/logoff tests.
