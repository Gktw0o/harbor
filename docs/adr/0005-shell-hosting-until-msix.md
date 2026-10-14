# 5. Running Harbor.Shell before MSIX packaging

- Status: Accepted (revisit in M7, packaging)
- Date: 2026-10-14

## Context

Harbor.Shell is a WinUI 3 app (Windows App SDK). MSIX packaging is planned for M7. Until then developers
and CI must build and launch it on x64 and ARM64 without installing anything extra.

## Decision

- **Unpackaged** (`WindowsPackageType=None`) with the **Windows App SDK runtime bundled**
  (`WindowsAppSDKSelfContained=true`), so `Harbor.Shell.exe` runs straight from the build output.
- Reference **`Microsoft.WindowsAppSDK.WinUI` only** (2.3.9, from the 2.5.1 release), not the
  `Microsoft.WindowsAppSDK` metapackage, which also pulls in AI, ML, Search and Widgets.
- Pin the transitive `Microsoft.WindowsAppSDK.InteractiveExperiences` to 2.1.9 with central package
  transitive pinning: WinUI 2.3.9 asks for 2.1.8, which was never published (NU1603).

Verified at M0: builds with `dotnet build` on an ARM64 host (the XAML compiler works there) and launches on
x64 and ARM64, per-monitor-v2 DPI aware.

## Consequences

- Larger build output than a framework-dependent app; irrelevant until packaging.
- `dotnet format`'s analyzers pass can't see XAML-generated code, so formatting checks run
  `dotnet format whitespace` and `style`; analyzers already run as errors during the build.
- M7 replaces this with MSIX (all three executables, full-trust dock) and should drop the bundled runtime
  in favour of the framework package.
