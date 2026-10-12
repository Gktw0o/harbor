# Active Context

## Current focus
Milestone **M0 — Skeleton**: repo layout, both build systems, CI for x64 + ARM64,
an empty dock window and an empty Shell app that connect over a named pipe and exchange `ping`/`pong`.

## Recent changes
- Project initialized with Memory Bank and CLAUDE.md.

## Next steps
1. Scaffold the repo layout from `techContext.md`.
2. CMake presets for x64/arm64; vcpkg manifest; a "hello" dock window.
3. `Harbor.sln` with Shell, Sensors, Core; build for both RIDs.
4. `contracts/ping.schema.json` + IPC on both sides + contract tests.
5. GitHub Actions matrix.

## Open decisions
- Final product name.
- JSON library for C++ (nlohmann-json for ergonomics vs simdjson for speed).
- Tray icon strategy (see progress.md risks).
