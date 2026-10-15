# CI only: leaves the runner's desktop as a person's would be, so the tests that bring windows to the
# front can. The windows-11-arm runner starts with Windows' first-sign-in screen (WWAHost.exe) above every
# window, and closing it leaves Start/Search in front; Esc closes those (M3 probe, docs/adr/0010).
# windows-2025 needs nothing, and this does nothing harmful there.
$ErrorActionPreference = 'Stop'
if ($env:CI -ne 'true') {
    throw 'prepare-ci-desktop.ps1 changes the desktop: CI only.'
}

$setup = @(Get-Process -Name WWAHost -ErrorAction SilentlyContinue)
Write-Host "first-sign-in screen processes: $($setup.Count)"
$setup | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
(New-Object -ComObject WScript.Shell).SendKeys('{ESC}')
Start-Sleep -Seconds 1

Add-Type -Namespace Harbor -Name Desktop -MemberDefinition @'
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(System.IntPtr window, System.Text.StringBuilder name, int size);
'@
$name = New-Object System.Text.StringBuilder 256
[void][Harbor.Desktop]::GetClassName([Harbor.Desktop]::GetForegroundWindow(), $name, 256)
Write-Host "in front now: $name"
