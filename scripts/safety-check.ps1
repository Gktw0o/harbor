<#
.SYNOPSIS
    Checks Harbor's taskbar safety against the REAL taskbar of this PC. Local only; never run in CI.

.DESCRIPTION
    Starts the Release build of Harbor Shell (which starts the dock), turns "Hide the Windows taskbar" on
    through UI Automation, breaks things in one way per scenario, and checks that the taskbar comes back,
    and how fast. Your taskbar WILL disappear for a few seconds at a time while it runs; save your work
    first. It records the taskbar's state (visibility, auto-hide) before anything else and puts it back at
    the end, whatever happens.

    Build first: scripts\build.ps1 -Arch <arch> -Config Release

    Scenarios (all by default):
      panic        Ctrl+Alt+Shift+H restores; Harbor Shell turns hiding off.
      kill-dock    taskkill /F on the dock: the Shell's watchdog restores.
      freeze-dock  the dock is suspended (every thread): the watchdog restores and ends it.
      close-shell  Harbor Shell closes: the dock restores by itself.
      kill-shell   Harbor Shell is killed: the dock restores by itself.
      kill-both    both are killed: nothing can restore until Harbor Shell starts again; it does then.
      start-menu   the Win key opens Start and Esc closes it: the taskbar stays hidden (re-hides counted).
      idle         the dock's CPU and memory with the taskbar hidden (rule 4: < 0.5 % CPU, < 40 MB).
      explorer     Explorer restarts (only with -IncludeExplorerRestart; closes File Explorer windows):
                   the dock hides the new taskbar again.
      appbar-kill     the dock's screen strip (AppBar) is reserved while it runs; with Harbor Shell closed and
                      the dock killed, Windows frees it (maximized windows reach the taskbar again).
      appbar-restart  the dock is killed and Harbor Shell starts it again: one strip, not two.
      stop-dock       "Stop dock" in Harbor Shell: the dock closes, the taskbar and the strip come back, and
                      the dock isn't started again.

.PARAMETER ExerciseAutoHide
    If your taskbar's auto-hide is on, turn it off for the run, so Harbor has to turn it on and restore it
    from its record. Your setting is put back at the end.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\safety-check.ps1 -Arch arm64 -ExerciseAutoHide
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('x64', 'arm64')]
    [string] $Arch,

    [ValidateSet('panic', 'kill-dock', 'freeze-dock', 'close-shell', 'kill-shell', 'kill-both', 'start-menu', 'idle', 'explorer',
        'appbar-kill', 'appbar-restart', 'stop-dock')]
    [string[]] $Scenario = @('panic', 'kill-dock', 'freeze-dock', 'close-shell', 'kill-shell', 'kill-both', 'start-menu', 'idle',
        'appbar-kill', 'appbar-restart', 'stop-dock'),

    [switch] $ExerciseAutoHide,

    [switch] $IncludeExplorerRestart,

    [int] $IdleSeconds = 30
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:CI) {
    throw 'safety-check.ps1 works on the real taskbar and never runs in CI.'
}
if ($IncludeExplorerRestart -and $Scenario -notcontains 'explorer') {
    $Scenario += 'explorer'
}
if ($Scenario -contains 'explorer' -and -not $IncludeExplorerRestart) {
    throw 'The explorer scenario restarts Windows Explorer: pass -IncludeExplorerRestart to allow it.'
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$platform = if ($Arch -eq 'arm64') { 'ARM64' } else { 'x64' }
$shellDir = Join-Path $repoRoot "src\Harbor.Shell\bin\$platform\Release\net10.0-windows10.0.26100.0\win-$Arch"
$shellExe = Join-Path $shellDir 'Harbor.Shell.exe'
foreach ($required in @($shellExe, (Join-Path $shellDir 'harbor-dock.exe'))) {
    if (-not (Test-Path $required)) {
        throw "$required not found. Build first: scripts\build.ps1 -Arch $Arch -Config Release"
    }
}
if (Get-Process -Name 'Harbor.Shell', 'harbor-dock' -ErrorAction SilentlyContinue) {
    throw 'Harbor is already running. Close Harbor Shell and harbor-dock first.'
}

$dataDir = Join-Path $env:LOCALAPPDATA 'Harbor'
$recordPath = Join-Path $dataDir 'taskbar-restore.json'
$dockLog = Join-Path $dataDir 'logs\dock.log'

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type -Namespace HarborSafety -Name Native -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string windowClass, string title);
[DllImport("user32.dll")] static extern IntPtr GetShellWindow();
[DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
[DllImport("user32.dll")] static extern bool ShowWindowAsync(IntPtr window, int command);
[DllImport("user32.dll")] static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
[DllImport("ntdll.dll")] public static extern int NtSuspendProcess(IntPtr process);
[StructLayout(LayoutKind.Sequential)] struct Rect { public int Left, Top, Right, Bottom; }
[StructLayout(LayoutKind.Sequential)] struct AppBarData { public int Size; public IntPtr Window; public uint Callback; public uint Edge; public Rect Bounds; public IntPtr Param; }
[DllImport("shell32.dll")] static extern UIntPtr SHAppBarMessage(uint message, ref AppBarData data);

// Explorer's taskbar windows (primary first), owned by the shell process.
public static IntPtr[] TaskbarWindows() {
    var windows = new System.Collections.Generic.List<IntPtr>();
    uint shell;
    if (GetWindowThreadProcessId(GetShellWindow(), out shell) == 0) return windows.ToArray();
    foreach (var windowClass in new[] { "Shell_TrayWnd", "Shell_SecondaryTrayWnd" }) {
        var window = IntPtr.Zero;
        while ((window = FindWindowEx(IntPtr.Zero, window, windowClass, null)) != IntPtr.Zero) {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == shell) windows.Add(window);
        }
    }
    return windows.ToArray();
}
public static bool PrimaryVisible() { var w = TaskbarWindows(); return w.Length > 0 && IsWindowVisible(w[0]); }
public static bool PrimaryHidden() { var w = TaskbarWindows(); return w.Length > 0 && !IsWindowVisible(w[0]); }
public static void ShowAll() { foreach (var w in TaskbarWindows()) ShowWindowAsync(w, 8); }
public static uint AutoHide() { var d = new AppBarData(); d.Size = Marshal.SizeOf(d); return (uint)SHAppBarMessage(4, ref d) & 1; }
public static void SetAutoHide(uint state) {
    var d = new AppBarData(); d.Size = Marshal.SizeOf(d);
    var w = TaskbarWindows(); d.Window = w.Length > 0 ? w[0] : IntPtr.Zero; d.Param = (IntPtr)state;
    SHAppBarMessage(10, ref d);
}
public static void PressKeys(params byte[] keys) {
    foreach (var key in keys) keybd_event(key, 0, 0, UIntPtr.Zero);
    for (var i = keys.Length - 1; i >= 0; i--) keybd_event(keys[i], 0, 2, UIntPtr.Zero);
}
[DllImport("user32.dll")] static extern bool SystemParametersInfo(uint action, uint param, ref Rect rect, uint winIni);
[DllImport("user32.dll")] static extern IntPtr MonitorFromPoint(long point, uint flags);
[StructLayout(LayoutKind.Sequential)] struct MonitorInfo { public int Size; public Rect Monitor; public Rect Work; public uint Flags; }
[DllImport("user32.dll")] static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);
[DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
// The primary monitor's work area and full bottom edge, in physical pixels (call PerMonitorAware first).
public static int WorkBottom() { var r = new Rect(); SystemParametersInfo(0x30, 0, ref r, 0); return r.Bottom; }
public static int MonitorBottom() { var i = new MonitorInfo(); i.Size = Marshal.SizeOf(i); GetMonitorInfo(MonitorFromPoint(0, 1), ref i); return i.Monitor.Bottom; }
public static void PerMonitorAware() { SetThreadDpiAwarenessContext((IntPtr)(-4)); }
'@
# Physical pixels for the work-area checks, like the dock's own numbers.
[HarborSafety.Native]::PerMonitorAware() | Out-Null

function Wait-Until {
    param([scriptblock] $Condition, [int] $TimeoutMs)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.ElapsedMilliseconds -lt $TimeoutMs) {
        if (& $Condition) { return [int]$watch.ElapsedMilliseconds }
        Start-Sleep -Milliseconds 50
    }
    return -1
}

function Get-DockLogLines {
    if (-not (Test-Path $dockLog)) { return @() }
    $stream = [IO.File]::Open($dockLog, 'Open', 'Read', 'ReadWrite, Delete')
    try { return (New-Object IO.StreamReader($stream)).ReadToEnd() -split "`n" } finally { $stream.Dispose() }
}

function Get-ShellWindow {
    $condition = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::NameProperty, 'Harbor Shell')
    return [Windows.Automation.AutomationElement]::RootElement.FindFirst([Windows.Automation.TreeScope]::Children, $condition)
}

function Get-HideToggle {
    $window = Get-ShellWindow
    if ($null -eq $window) { return $null }
    $condition = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::NameProperty, 'Hide the Windows taskbar')
    return $window.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
}

function Get-ToggleIsOn {
    $toggle = Get-HideToggle
    if ($null -eq $toggle) { return $null }
    return $toggle.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -eq 'On'
}

function Start-Harbor {
    Start-Process -FilePath $shellExe | Out-Null
    if ((Wait-Until { $null -ne (Get-HideToggle) } 20000) -lt 0) { throw 'Harbor Shell did not open.' }
    if ((Wait-Until { $null -ne (Get-Process harbor-dock -ErrorAction SilentlyContinue) } 15000) -lt 0) {
        throw 'Harbor Shell did not start the dock.'
    }
}

function Enable-Hide {
    $toggle = Get-HideToggle
    $pattern = $toggle.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)
    if ($pattern.Current.ToggleState -ne 'On') { $pattern.Toggle() }
    if ((Wait-Until { [HarborSafety.Native]::PrimaryHidden() } 20000) -lt 0) {
        throw 'The taskbar did not hide after turning hiding on (see %LOCALAPPDATA%\Harbor\logs).'
    }
    # Let the auto-hide change land too.
    Start-Sleep -Milliseconds 500
}

# The dock reserved its strip: the work area ends above the bottom edge (the taskbar is hidden then).
function Wait-StripReserved {
    $ms = Wait-Until { [HarborSafety.Native]::WorkBottom() -lt [HarborSafety.Native]::MonitorBottom() - 20 } 10000
    if ($ms -lt 0) { throw 'The dock reserved no strip (the work area reaches the bottom of the screen).' }
    return [HarborSafety.Native]::MonitorBottom() - [HarborSafety.Native]::WorkBottom()
}

function Invoke-ShellButton {
    param([string] $Name)
    $condition = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::NameProperty, $Name)
    $button = (Get-ShellWindow).FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
    if ($null -eq $button) { throw "Harbor Shell has no '$Name' button." }
    $button.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
}

function Stop-Harbor {
    Get-Process -Name 'Harbor.Shell', 'harbor-dock' -ErrorAction SilentlyContinue | Stop-Process -Force
    Wait-Until { $null -eq (Get-Process -Name 'Harbor.Shell', 'harbor-dock' -ErrorAction SilentlyContinue) } 10000 | Out-Null
}

# The taskbar is back: its window visible and auto-hide as it was when the run started.
function Test-Restored {
    return [HarborSafety.Native]::PrimaryVisible() -and ([HarborSafety.Native]::AutoHide() -eq $script:runAutoHide)
}

# Harbor deletes its restore record once the restore is verified (about a second later). Only meaningful
# when Harbor had turned auto-hide on (-ExerciseAutoHide, or auto-hide off to begin with).
function Get-RecordNote {
    $gone = (Wait-Until { -not (Test-Path $recordPath) } 6000) -ge 0
    return @{ Gone = $gone; Text = "; record cleaned up by Harbor: $gone" }
}

# Puts the taskbar back exactly as the run started, without Harbor's help.
function Reset-Taskbar {
    [HarborSafety.Native]::ShowAll()
    if ([HarborSafety.Native]::AutoHide() -ne $script:runAutoHide) { [HarborSafety.Native]::SetAutoHide($script:runAutoHide) }
    if (Test-Path $recordPath) { Remove-Item $recordPath -Force }
}

$results = New-Object System.Collections.Generic.List[object]
function Add-Result {
    param([string] $Name, [bool] $Passed, [string] $Detail)
    $results.Add([pscustomobject]@{ Scenario = $Name; Result = $(if ($Passed) { 'PASS' } else { 'FAIL' }); Detail = $Detail })
    $color = if ($Passed) { 'Green' } else { 'Red' }
    Write-Host ("{0,-12} {1}  {2}" -f $Name, $(if ($Passed) { 'PASS' } else { 'FAIL' }), $Detail) -ForegroundColor $color
}

$userAutoHide = [HarborSafety.Native]::AutoHide()
if (-not [HarborSafety.Native]::PrimaryVisible()) {
    throw 'The taskbar is not visible right now; refusing to start from an unknown state.'
}
Write-Host "Taskbar before the run: visible, auto-hide $(if ($userAutoHide) { 'on' } else { 'off' })."
$script:runAutoHide = $userAutoHide
$script:runWorkBottom = [HarborSafety.Native]::WorkBottom()
try {
    if ($ExerciseAutoHide -and $userAutoHide -eq 1) {
        Write-Host 'Turning auto-hide off for the run (-ExerciseAutoHide).'
        [HarborSafety.Native]::SetAutoHide(0)
        $script:runAutoHide = 0
        Start-Sleep -Seconds 1
        $script:runWorkBottom = [HarborSafety.Native]::WorkBottom()
    }

    foreach ($name in $Scenario) {
        Write-Host "--- $name" -ForegroundColor Cyan
        try {
            Start-Harbor
            Enable-Hide
            $dock = Get-Process harbor-dock
            # Opens a handle now: without one, a Get-Process object can't tell the exit code later.
            $null = $dock.Handle
            switch ($name) {
                'panic' {
                    [HarborSafety.Native]::PressKeys(0x11, 0x12, 0x10, 0x48) # Ctrl, Alt, Shift, H
                    $ms = Wait-Until { Test-Restored } 5000
                    $toggleOff = (Wait-Until { (Get-ToggleIsOn) -eq $false } 3000) -ge 0
                    $record = Get-RecordNote
                    Add-Result $name ($ms -ge 0 -and $toggleOff -and $record.Gone) "restored in $ms ms; Shell turned hiding off: $toggleOff$($record.Text)"
                }
                'kill-dock' {
                    Stop-Process -Id $dock.Id -Force
                    $ms = Wait-Until { Test-Restored } 8000
                    $record = Get-RecordNote
                    Add-Result $name ($ms -ge 0 -and $record.Gone) "restored $ms ms after the kill$($record.Text)"
                }
                'freeze-dock' {
                    [void][HarborSafety.Native]::NtSuspendProcess($dock.Handle)
                    $ms = Wait-Until { Test-Restored } 15000
                    $ended = (Wait-Until { $dock.HasExited } 5000) -ge 0
                    $record = Get-RecordNote
                    Add-Result $name ($ms -ge 0 -and $ended -and $record.Gone) "restored $ms ms after freezing; dock ended: $ended$($record.Text)"
                }
                'close-shell' {
                    $window = Get-ShellWindow
                    $window.GetCurrentPattern([Windows.Automation.WindowPattern]::Pattern).Close()
                    $ms = Wait-Until { Test-Restored } 5000
                    $dockAlive = -not $dock.HasExited
                    $record = Get-RecordNote
                    Add-Result $name ($ms -ge 0 -and $record.Gone) "restored by the dock in $ms ms; dock still running: $dockAlive$($record.Text)"
                }
                'kill-shell' {
                    Get-Process Harbor.Shell | Stop-Process -Force
                    $ms = Wait-Until { Test-Restored } 5000
                    $record = Get-RecordNote
                    Add-Result $name ($ms -ge 0 -and $record.Gone) "restored by the dock in $ms ms$($record.Text)"
                }
                'kill-both' {
                    Stop-Harbor
                    Start-Sleep -Seconds 2
                    $stillHidden = [HarborSafety.Native]::PrimaryHidden()
                    Start-Process -FilePath $shellExe | Out-Null
                    $ms = Wait-Until { Test-Restored } 15000
                    $record = Get-RecordNote
                    Add-Result $name ($ms -ge 0 -and $record.Gone) "hidden with nothing left to restore: $stillHidden; Harbor Shell's start restored it in $ms ms$($record.Text)"
                }
                'start-menu' {
                    $before = @(Get-DockLogLines | Where-Object { $_ -match 'Explorer showed it again' }).Count
                    [HarborSafety.Native]::PressKeys(0x5B) # Win
                    Start-Sleep -Milliseconds 1500
                    [HarborSafety.Native]::PressKeys(0x1B) # Esc
                    Start-Sleep -Milliseconds 1500
                    $rehides = @(Get-DockLogLines | Where-Object { $_ -match 'Explorer showed it again' }).Count - $before
                    $hidden = [HarborSafety.Native]::PrimaryHidden()
                    $tripped = @(Get-DockLogLines | Where-Object { $_ -match 'keeps showing the taskbar' }).Count -gt 0
                    Add-Result $name ($hidden -and -not $tripped) "still hidden: $hidden; re-hides: $rehides; breaker tripped: $tripped"
                }
                'idle' {
                    $dock.Refresh()
                    $cpuBefore = $dock.TotalProcessorTime
                    Start-Sleep -Seconds $IdleSeconds
                    $dock.Refresh()
                    $cpuPercent = ($dock.TotalProcessorTime - $cpuBefore).TotalMilliseconds / ($IdleSeconds * 1000 * [Environment]::ProcessorCount) * 100
                    $workingSetMb = $dock.WorkingSet64 / 1MB
                    $privateMb = $dock.PrivateMemorySize64 / 1MB
                    $passed = $cpuPercent -lt 0.5 -and $workingSetMb -lt 40
                    Add-Result $name $passed ("{0:N3} % CPU over {1} s, {2:N1} MB working set, {3:N1} MB private" -f $cpuPercent, $IdleSeconds, $workingSetMb, $privateMb)
                }
                'appbar-kill' {
                    $strip = Wait-StripReserved
                    # Without Harbor Shell nothing restarts the dock (or sends ABM_REMOVE for it): Windows alone
                    # must free the strip of a dock that died.
                    $window = Get-ShellWindow
                    $window.GetCurrentPattern([Windows.Automation.WindowPattern]::Pattern).Close()
                    Wait-Until { Test-Restored } 5000 | Out-Null
                    Stop-Process -Id $dock.Id -Force
                    $ms = Wait-Until { [HarborSafety.Native]::WorkBottom() -eq $script:runWorkBottom } 5000
                    Add-Result $name ($ms -ge 0) "strip of $strip px freed $ms ms after the kill (work area bottom $([HarborSafety.Native]::WorkBottom()), was $script:runWorkBottom)"
                }
                'appbar-restart' {
                    $strip = Wait-StripReserved
                    Stop-Process -Id $dock.Id -Force
                    # Hiding stays off after a crash: the new dock's strip goes above the visible taskbar.
                    $restarted = (Wait-Until { $null -ne (Get-Process harbor-dock -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $dock.Id }) } 15000) -ge 0
                    $expected = $script:runWorkBottom - $strip
                    $ms = Wait-Until { [Math]::Abs([HarborSafety.Native]::WorkBottom() - $expected) -le 1 } 10000
                    Add-Result $name ($restarted -and $ms -ge 0) "restarted: $restarted; work area bottom $([HarborSafety.Native]::WorkBottom()), one strip would be $expected"
                }
                'stop-dock' {
                    $strip = Wait-StripReserved
                    Invoke-ShellButton 'Stop dock'
                    $exited = (Wait-Until { $dock.HasExited } 10000) -ge 0
                    $ms = Wait-Until { (Test-Restored) -and [HarborSafety.Native]::WorkBottom() -eq $script:runWorkBottom } 8000
                    Start-Sleep -Seconds 3
                    $stayedStopped = $null -eq (Get-Process harbor-dock -ErrorAction SilentlyContinue)
                    $code = if ($exited) { $dock.ExitCode } else { 'n/a' }
                    Add-Result $name ($exited -and $code -eq 0 -and $ms -ge 0 -and $stayedStopped) "exit code $code; taskbar and $strip px strip back in $ms ms; stayed stopped: $stayedStopped"
                }
                'explorer' {
                    Get-Process explorer | Stop-Process -Force
                    # Windows starts Explorer again by itself; start it if it doesn't.
                    if ((Wait-Until { $null -ne (Get-Process explorer -ErrorAction SilentlyContinue) } 5000) -lt 0) { Start-Process explorer.exe }
                    $ms = Wait-Until { [HarborSafety.Native]::PrimaryHidden() } 20000
                    $noticed = @(Get-DockLogLines | Where-Object { $_ -match 'Explorer restarted' }).Count -gt 0
                    Add-Result $name ($ms -ge 0 -and $noticed) "new taskbar hidden again after $ms ms; dock saw the restart: $noticed"
                }
            }
        }
        catch {
            Add-Result $name $false "error: $($_.Exception.Message)"
        }
        finally {
            Stop-Harbor
            Reset-Taskbar
            Start-Sleep -Milliseconds 500
        }
    }
}
finally {
    Stop-Harbor
    [HarborSafety.Native]::ShowAll()
    if ([HarborSafety.Native]::AutoHide() -ne $userAutoHide) { [HarborSafety.Native]::SetAutoHide($userAutoHide) }
    if (Test-Path $recordPath) { Remove-Item $recordPath -Force }
    $after = [HarborSafety.Native]::AutoHide()
    Write-Host "Taskbar after the run: visible $([HarborSafety.Native]::PrimaryVisible()), auto-hide $(if ($after) { 'on' } else { 'off' }) (was $(if ($userAutoHide) { 'on' } else { 'off' }))."
}

$results | Format-Table -AutoSize
if ($results | Where-Object { $_.Result -eq 'FAIL' }) { exit 1 }
