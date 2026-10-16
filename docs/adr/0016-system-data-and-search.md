# 16. Where the island's data comes from: apps, stats, power, media, controls

- Status: Accepted
- Date: 2026-10-16
- Amends: ADR 0008 (glass gating, see ADR 0013); the system patterns' "Sensors owns all polling"

## Context

The island's M4 sections (ADR 0013) need data and actions from Windows:
- **Apps + search**: the installed apps;
- **Stats**: CPU (total and per core), memory, GPU, drives, battery;
- **Power**: lock, sign out, sleep, hibernate, restart, shut down;
- **Media**: what is playing, and its controls;
- **Controls**: volume, brightness, Wi-Fi/Bluetooth, light/dark mode.

Harbor is unpackaged until MSIX (ADR 0005), and some Windows APIs need package identity. The original plan had
a `Harbor.Sensors` process stream stats to the dock at 1 Hz, all the time.

The M4 spike, read-only on ARM64 (Snapdragon, Turkish UI), from an unpackaged process:

| Source | API | Result |
|---|---|---|
| Media sessions | `GlobalSystemMediaTransportControlsSessionManager` | Sessions, playback state and controls, properties with thumbnail |
| Volume | Core Audio `IAudioEndpointVolume` | Readable |
| Radios | `Windows.Devices.Radios` | Wi-Fi and Bluetooth with their state, no consent prompt |
| Battery | `GetSystemPowerStatus` and `AggregateBattery` | Both work |
| GPU | PDH `\GPU Engine(*)\Utilization Percentage`, added by its English name (`PdhAddEnglishCounter`) under the Turkish UI | 364 instances, 8 engine types, 4 ms per collect |
| Per-core CPU | PDH `\Processor Information(*)\% Processor Utility` | 20 instances |
| Brightness | WMI `WmiMonitorBrightness` | The built-in display; settable |
| Power | `GetPwrCapabilities` | No S3, hibernate available, **Modern Standby** |
| Apps | `shell:AppsFolder` enumeration | 207 apps in 1.1 s cold, about 25 KB of names and ids |

## Decision

**Apps + search.**
- An index of `shell:AppsFolder`: display name and parsing name (the AppUserModelID or path). It is built off
  the UI thread when the Shell starts and refreshed when apps are installed or removed. Names only, about
  25 KB.
- **Scoring is pure** (`AppSearch` in Core): prefix, word start, acronym, then substring. Matching is
  culture-aware with diacritics removed, and handles the Turkish ı/İ.
- An `ISearchProvider` port lets later sources plug in: files, then web plugins and a plugin store.
- Icons come lazily from the Shell's existing icon code, in a small cache.
- **Launching goes through the dock**, with a new `launch` message (ADR 0009's Explorer route), so apps don't
  become the Shell's children. When no dock is connected, the Shell falls back to `ShellExecuteEx`.
- An entry opens Windows' Start (Ctrl+Esc, ADR 0015). A result can be pinned to the dock (`PinChanges`).

**Stats, sampled by the Shell only while Stats is shown**, at 1 Hz:
- `GetSystemTimes` for total CPU, PDH English counters for per-core CPU and GPU engines, `GlobalMemoryStatusEx`,
  `GetDiskFreeSpaceEx` per fixed drive, and battery state, time left and charge rate.
- Every source reports "not available" instead of throwing (rule 2): GPU counters on a driver without them,
  no battery.
- The PDH query is closed 30 s after Stats is hidden. Sampling costs under 1 % CPU.
- **`Harbor.Sensors` stays**, as the host for M6's optional elevated temperature backend (rule 3). The dock
  polls nothing (rule 4).

**Power**, behind an `IPowerActions` port:
- `LockWorkStation`;
- sign out (`ExitWindowsEx`);
- **Sleep**: on Modern Standby PCs it turns the displays off (`WM_SYSCOMMAND`/`SC_MONITORPOWER`), which lets
  the PC enter modern standby as the power button does. `SetSuspendState` is used only where S3 exists.
- Hibernate (only if enabled), restart and shut down (`InitiateShutdown`).
- Restart, shut down and sign out ask for confirmation in the island. Sign-out and shutdown reach the dock's
  tested session-end restore (rule 1).
- Tests never run them: a fake records what was asked.

**Media.**
- The media sessions API, events only, no polling.
- Title, artist and artwork are shown, with play/pause, previous/next and progress. Artwork is decoded small
  and only while shown.
- The collapsed notch stays empty, as the user chose; media appears in the bubble and the panel.
- Titles are never logged.

**Controls.** Each control is hidden when its source is unavailable.
- **Volume and mute** through Core Audio. The change callback is registered only while Controls is shown.
- **Brightness** through WMI for built-in displays; DDC/CI for external ones later.
- **Wi-Fi and Bluetooth** through `Windows.Devices.Radios`.
- **Light/dark mode**, the **one undocumented piece**, approved here by the user (rule 6):
  - It writes `AppsUseLightTheme` and `SystemUsesLightTheme` under `HKCU\…\Themes\Personalize`, then
    broadcasts `WM_SETTINGCHANGE` "ImmersiveColorSet" with `SendMessageTimeout(SMTO_ABORTIFHUNG)` off the UI
    thread, as Auto Dark Mode does.
  - If a Windows update changes how this works, the switch is removed and the link below remains.
- **Focus, night light, airplane mode and power mode** have no public API. They are shortcuts that open the
  right Settings page (`ms-settings:`).

**Glass gating** (ADR 0013): the dock's appearance also reads Windows' effective power mode. Under energy
saver the dock uses its solid tint instead of drawing the host backdrop, which is black then (M2 checklist
2.1).

## Alternatives considered

- **A Sensors process streaming to the dock all the time**: the stats now live in the island, which samples
  only while they are shown; a process running at idle costs memory for nothing.
- **Packaging now for identity-only APIs** (notifications): that is MSIX work (M7); Notifications follow it.
- **Windows Search (OLE DB) for apps**: a heavier dependency for a list that `AppsFolder` gives directly.
  Files will use it later.
- **Undocumented night light and focus toggles** (registry blobs): fragile, and not approved.

## Consequences

- Harbor Shell gains WinRT, COM and PDH dependencies, in `Harbor.Platform`. `Harbor.Core` stays pure.
- The light/dark switch can break with a Windows update; it is isolated and removable.
- Power actions are destructive by nature: the confirmations, and the fact that tests never execute them,
  are the guard.
