# Progress

## Milestones
- [ ] **M0 Skeleton** — layout, builds for x64+arm64, CI, ping/pong IPC.
- [ ] **M1 Safety first** — taskbar hide/restore + panic hotkey + crash filter + Shell watchdog,
      with a manual test checklist (kill -9, logoff, explorer restart, monitor hot-plug).
- [ ] **M2 Dock window** — AppBar registration, per-monitor DPI, backdrop + rounded island,
      pinned apps from settings, launch on click.
- [ ] **M3 Running windows** — event-hook tracking, grouping by AUMID, activate/minimize,
      running indicators, icons cache, multi-monitor.
- [ ] **M4 Sensors baseline** — CPU/RAM/storage/GPU/battery backends, stats stream to dock,
      stats island UI with hover-expand.
- [ ] **M5 Settings + onboarding** — WinUI 3 settings, first-run explainer, autostart.
- [ ] **M6 Temperatures** — capability detection per arch, ACPI baseline,
      optional elevated backend (spike + ADR first).
- [ ] **M7 Polish + packaging** — animations (magnify, bounce), auto-hide, MSIX, perf budget check.
- [ ] **v2 Finder** — scoped separately when v1 ships.

## What works
- Nothing yet.

## Known issues
- None yet.

## Risks / research spikes
- **Tray icons**: no public API to enumerate other apps' notification icons.
  Options to evaluate in a spike: keep a minimal system tray visible, or show a "tray" button
  that reveals the real one. Decide via ADR before M3 ends.
- **Temps on ARM64**: may be unavailable; UI must handle it.
- **Explorer re-showing the taskbar**: handle `TaskbarCreated`, display and DPI changes.
