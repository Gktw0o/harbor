# 2. Building x64 and ARM64 with one command

- Status: Accepted
- Date: 2026-10-14

## Context

Every change must build and test on x64 and ARM64. Developers use an x64 desktop and an ARM64 laptop;
CI has native runners for both. The C++ dock needs MSVC for the target architecture, and the .NET
solution needs a runtime identifier per architecture.

## Decision

- **C++**: CMake presets `msvc-x64` / `msvc-arm64` with the **Ninja Multi-Config** generator, vcpkg in
  manifest mode with the registry baseline pinned in `vcpkg-configuration.json` (works with the
  vcpkg bundled in Visual Studio and with a standalone one), triplets `*-windows-static-md` (static
  libraries, shared CRT). CMake fails configuration if the compiler's architecture differs from the
  preset's.
- **.NET**: `Harbor.slnx` maps the `x64` and `ARM64` solution platforms to every project, and
  `Directory.Build.props` maps the platform to `win-x64` / `win-arm64`. Build with
  `-p:Platform=...`; `-r`/`-a` on a solution fail with NETSDK1134.
- **`scripts/build.ps1 -Arch x64|arm64`** enters the Visual Studio developer environment for the target
  (host detected from the registry, so an emulated shell doesn't mislead it), builds C++ and .NET,
  checks the PE machine type of the dock, runs all tests, and restores the caller's environment. CI runs
  the same script. It refuses to run ARM64 tests on x64 hardware instead of silently skipping them.

## Alternatives considered

- **Visual Studio generator**: no developer shell needed, but slower, tied to a VS version, and it produces
  no `compile_commands.json` for clangd.
- **Separate per-tool scripts or raw commands in CI**: CI and local builds would drift.

## Consequences

- Plain `cmake --preset` commands need a developer shell for the right architecture; the script and the
  configure-time guard cover that.
- On an x64 machine, ARM64 builds but can't be tested; the CI job `arm64 (cross-compiled on x64)` mirrors
  that, and the native `windows-11-arm` job runs the ARM64 tests.
