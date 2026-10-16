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
      M3 (these move the mouse and click Harbor Shell's title bar):
      peek         the island's tray button: Explorer slides the taskbar in and the dock leaves it; once the
                   user clicks elsewhere and it slides out, the dock hides it again (no breaker trip).
      peek-panic   Ctrl+Alt+Shift+H while peeking restores.
      peek-kill    the dock killed while peeking: the watchdog restores.
      attention    a window (harbor_test_windows, off screen) flashes: the dock marks its app.
      menu-panic   a right-click menu open on the island, then Ctrl+Alt+Shift+H: the menu closes and the
                   taskbar comes back.
      island-move  (two monitors) resting the cursor on the other monitor's bottom edge moves the island
                   there and back; the taskbar stays hidden throughout. SKIP with one monitor.
      budget       (only when named) the dock's memory and GUI handles with 0 and 30 running apps, after a
                   label and a menu, after 50 monitor moves, after 10,000 windows came and went.

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

    # Checked below: with -File, "-Scenario a,b" arrives as one string.
    [string[]] $Scenario = @('panic', 'kill-dock', 'freeze-dock', 'close-shell', 'kill-shell', 'kill-both', 'start-menu', 'idle',
        'appbar-kill', 'appbar-restart', 'stop-dock', 'peek', 'peek-panic', 'peek-kill', 'attention', 'menu-panic', 'island-move'),

    [switch] $ExerciseAutoHide,

    [switch] $IncludeExplorerRestart,

    [int] $IdleSeconds = 30
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:CI) {
    throw 'safety-check.ps1 works on the real taskbar and never runs in CI.'
}
$knownScenarios = @('panic', 'kill-dock', 'freeze-dock', 'close-shell', 'kill-shell', 'kill-both', 'start-menu', 'idle', 'explorer',
    'appbar-kill', 'appbar-restart', 'stop-dock', 'peek', 'peek-panic', 'peek-kill', 'attention', 'menu-panic', 'island-move', 'budget')
$Scenario = @($Scenario | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($name in $Scenario) {
    if ($knownScenarios -notcontains $name) { throw "Unknown scenario '$name'. Known: $($knownScenarios -join ', ')" }
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
public static bool AllVisible() { var w = TaskbarWindows(); foreach (var x in w) if (!IsWindowVisible(x)) return false; return w.Length > 0; }
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
// M3: the island, its menus, the mouse, monitors and GUI handle counts.
[DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out Rect rect);
[DllImport("user32.dll")] static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
[DllImport("user32.dll")] static extern IntPtr WindowFromPoint(long point);
[DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr window, uint flags);
[DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
delegate bool MonitorCallback(IntPtr monitor, IntPtr dc, ref Rect rect, IntPtr data);
[DllImport("user32.dll")] static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr clip, MonitorCallback callback, IntPtr data);
// A visible top-level window of that class owned by the process, or zero.
public static IntPtr WindowOf(uint processId, string windowClass) {
    var window = IntPtr.Zero;
    while ((window = FindWindowEx(IntPtr.Zero, window, windowClass, null)) != IntPtr.Zero) {
        uint owner;
        GetWindowThreadProcessId(window, out owner);
        if (owner == processId && IsWindowVisible(window)) return window;
    }
    return IntPtr.Zero;
}
// left, top, right, bottom.
public static int[] RectOf(IntPtr window) { Rect r; GetWindowRect(window, out r); return new[] { r.Left, r.Top, r.Right, r.Bottom }; }
public static int[][] Monitors() {
    var list = new System.Collections.Generic.List<int[]>();
    EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (IntPtr m, IntPtr dc, ref Rect r, IntPtr d) => { list.Add(new[] { r.Left, r.Top, r.Right, r.Bottom }); return true; }, IntPtr.Zero);
    return list.ToArray();
}
// Explorer shows the primary taskbar: visible, and at least half of it on its monitor (auto-hide slides it away).
public static bool PrimarySlidIn() {
    var w = TaskbarWindows();
    if (w.Length == 0 || !IsWindowVisible(w[0])) return false;
    Rect r; GetWindowRect(w[0], out r);
    var i = new MonitorInfo(); i.Size = Marshal.SizeOf(i); GetMonitorInfo(MonitorFromWindow(w[0], 2), ref i);
    var visible = Math.Min(r.Bottom, i.Monitor.Bottom) - Math.Max(r.Top, i.Monitor.Top);
    return 2 * visible >= r.Bottom - r.Top;
}
// The process owning the top-level window under the point.
public static uint ProcessAt(int x, int y) {
    var root = GetAncestor(WindowFromPoint(((long)y << 32) | (uint)x), 2);
    uint process; GetWindowThreadProcessId(root, out process); return process;
}
public static void Click(bool right) {
    mouse_event(right ? 0x0008u : 0x0002u, 0, 0, 0, UIntPtr.Zero);
    mouse_event(right ? 0x0010u : 0x0004u, 0, 0, 0, UIntPtr.Zero);
}
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
    param([string] $Name, [bool] $Passed, [string] $Detail, [switch] $Skipped)
    $result = if ($Skipped) { 'SKIP' } elseif ($Passed) { 'PASS' } else { 'FAIL' }
    $results.Add([pscustomobject]@{ Scenario = $Name; Result = $result; Detail = $Detail })
    $color = if ($Skipped) { 'Yellow' } elseif ($Passed) { 'Green' } else { 'Red' }
    Write-Host ("{0,-12} {1}  {2}" -f $Name, $result, $Detail) -ForegroundColor $color
}

# M3 (docs/testing/m3-checklist.md): the island, its tray button, menus, attention and monitors.

function Get-DockLogCount {
    param([string] $Pattern)
    return @(Get-DockLogLines | Where-Object { $_ -match $Pattern }).Count
}

function Get-Island {
    param($Dock)
    $island = [HarborSafety.Native]::WindowOf([uint32]$Dock.Id, 'Harbor.Dock.Island')
    if ($island -eq [IntPtr]::Zero) { throw 'The dock shows no island.' }
    return $island
}

# The dock's tray button ("Show taskbar"), through UI Automation like a screen reader.
function Invoke-TrayButton {
    param($Dock)
    # Not $condition: Wait-Until's own parameter would shadow it inside the block.
    $byName = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::NameProperty, 'Show taskbar')
    $island = [Windows.Automation.AutomationElement]::FromHandle((Get-Island $Dock))
    $script:button = $null
    Wait-Until { $script:button = $island.FindFirst([Windows.Automation.TreeScope]::Descendants, $byName); $null -ne $script:button } 5000 | Out-Null
    if ($null -eq $script:button) { throw 'The island has no "Show taskbar" button.' }
    $script:button.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
}

# The user moves on: a click on Harbor Shell's title bar (checked to be Harbor Shell's before clicking).
function Move-FocusToShell {
    $rect = (Get-ShellWindow).Current.BoundingRectangle
    $x = [int]($rect.Left + 60); $y = [int]($rect.Top + 12)
    $shell = Get-Process Harbor.Shell
    if ([HarborSafety.Native]::ProcessAt($x, $y) -ne [uint32]$shell.Id) { throw "Harbor Shell's title bar is covered." }
    [void][HarborSafety.Native]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 100
    [HarborSafety.Native]::Click($false)
}

# Peeks and waits until Explorer shows the taskbar for it.
function Start-Peek {
    param($Dock)
    Invoke-TrayButton $Dock
    $ms = Wait-Until { [HarborSafety.Native]::PrimarySlidIn() } 5000
    if ($ms -lt 0) { throw 'The taskbar did not slide in for the peek.' }
    return $ms
}

# The middle of the island's first item row (any point of the island belongs to an item).
function Get-IslandPoint {
    param($Dock)
    $r = [HarborSafety.Native]::RectOf((Get-Island $Dock))
    return @([int](($r[0] + $r[2]) / 2), [int](($r[1] + $r[3]) / 2))
}

function Get-MonitorOf {
    param([int[]] $Rect)
    $x = ($Rect[0] + $Rect[2]) / 2; $y = ($Rect[1] + $Rect[3]) / 2
    foreach ($m in [HarborSafety.Native]::Monitors()) { if ($x -ge $m[0] -and $x -lt $m[2] -and $y -ge $m[1] -and $y -lt $m[3]) { return $m } }
    return $null
}

# Rests the cursor on a monitor's bottom row (the dock's edge trigger), long enough for the island to come.
function Invoke-EdgeTrigger {
    param([int[]] $Monitor)
    [void][HarborSafety.Native]::SetCursorPos([int](($Monitor[0] + $Monitor[2]) / 2), $Monitor[3] - 1)
    Start-Sleep -Milliseconds 900
}

function Get-Budget {
    param($Dock)
    $Dock.Refresh()
    $gdi = [HarborSafety.Native]::GetGuiResources($Dock.Handle, 0)
    $user = [HarborSafety.Native]::GetGuiResources($Dock.Handle, 1)
    return @{ WorkingSet = $Dock.WorkingSet64 / 1MB; Private = $Dock.PrivateMemorySize64 / 1MB; Handles = $Dock.HandleCount; Gdi = $gdi; User = $user;
        Text = ("{0:N1} MB working set, {1:N1} MB private, {2} handles, {3} GDI, {4} USER" -f ($Dock.WorkingSet64 / 1MB), ($Dock.PrivateMemorySize64 / 1MB), $Dock.HandleCount, $gdi, $user) }
}

# harbor_test_windows (Release): off-screen app windows the dock follows like any other, driven line by line.
$testWindowsExe = Join-Path $repoRoot "build\msvc-$Arch\tests\dock\Release\harbor_test_windows.exe"
function Start-TestWindows {
    if (-not (Test-Path $testWindowsExe)) { throw "$testWindowsExe not found. Build first: scripts\build.ps1 -Arch $Arch -Config Release" }
    $info = New-Object Diagnostics.ProcessStartInfo($testWindowsExe, 'safety')
    $info.UseShellExecute = $false; $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true
    $helper = [Diagnostics.Process]::Start($info)
    # Windows PowerShell's redirected input starts with a UTF-8 byte order mark: spend it on an empty line.
    $helper.StandardInput.WriteLine('')
    [void]$helper.StandardOutput.ReadLine()
    return $helper
}
function Send-TestWindows {
    param($Helper, [string] $Line)
    $Helper.StandardInput.WriteLine($Line)
    $answer = $Helper.StandardOutput.ReadLine()
    if ($answer -notmatch '^ok') { throw "harbor_test_windows: '$Line' -> '$answer'" }
}
function Stop-TestWindows {
    param($Helper)
    if ($null -eq $Helper) { return }
    try { $Helper.StandardInput.Close() } catch { }
    if (-not $Helper.WaitForExit(5000)) { $Helper.Kill() }
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
                'peek' {
                    $rehidesBefore = Get-DockLogCount 'Explorer showed it again'
                    $in = Start-Peek $dock
                    Start-Sleep -Seconds 2
                    $stayed = [HarborSafety.Native]::PrimarySlidIn()
                    Move-FocusToShell
                    $ms = Wait-Until { [HarborSafety.Native]::PrimaryHidden() } 8000
                    $over = (Get-DockLogCount 'the peek is over') -gt 0
                    $tripped = (Get-DockLogCount 'keeps showing the taskbar') -gt 0
                    $rehides = (Get-DockLogCount 'Explorer showed it again') - $rehidesBefore
                    $toggle = Get-ToggleIsOn
                    Add-Result $name ($stayed -and $ms -ge 0 -and $over -and -not $tripped -and $toggle) "slid in after $in ms; still in 2 s later: $stayed; hidden again $ms ms after a click elsewhere; breaker tripped: $tripped; re-hides: $rehides; hiding still on: $toggle"
                }
                'peek-panic' {
                    Start-Peek $dock | Out-Null
                    [HarborSafety.Native]::PressKeys(0x11, 0x12, 0x10, 0x48) # Ctrl, Alt, Shift, H
                    $ms = Wait-Until { (Test-Restored) -and [HarborSafety.Native]::AllVisible() } 5000
                    Start-Sleep -Seconds 2
                    $stayed = [HarborSafety.Native]::AllVisible()
                    $record = Get-RecordNote
                    Add-Result $name ($ms -ge 0 -and $stayed -and $record.Gone) "every taskbar restored in $ms ms; still shown 2 s later: $stayed$($record.Text)"
                }
                'peek-kill' {
                    Start-Peek $dock | Out-Null
                    Stop-Process -Id $dock.Id -Force
                    $ms = Wait-Until { (Test-Restored) -and [HarborSafety.Native]::AllVisible() } 8000
                    $record = Get-RecordNote
                    Add-Result $name ($ms -ge 0 -and $record.Gone) "every taskbar restored $ms ms after the kill$($record.Text)"
                }
                'attention' {
                    $helper = Start-TestWindows
                    try {
                        Send-TestWindows $helper 'new 1 Harbor.Safety.Attention'
                        Start-Sleep -Milliseconds 1500
                        $before = Get-DockLogCount 'asks for attention'
                        $rehidesBefore = Get-DockLogCount 'Explorer showed it again'
                        Send-TestWindows $helper 'flash 1'
                        $ms = Wait-Until { (Get-DockLogCount 'asks for attention') -gt $before } 5000
                        Start-Sleep -Seconds 2
                        $hidden = [HarborSafety.Native]::PrimaryHidden()
                        $tripped = (Get-DockLogCount 'keeps showing the taskbar') -gt 0
                        $rehides = (Get-DockLogCount 'Explorer showed it again') - $rehidesBefore
                        Add-Result $name ($ms -ge 0 -and $hidden -and -not $tripped) "marked $ms ms after the flash; taskbar hidden: $hidden; re-hides: $rehides; breaker tripped: $tripped"
                    }
                    finally { Stop-TestWindows $helper }
                }
                'menu-panic' {
                    $point = Get-IslandPoint $dock
                    if ([HarborSafety.Native]::ProcessAt($point[0], $point[1]) -ne [uint32]$dock.Id) { throw 'The island is covered.' }
                    [void][HarborSafety.Native]::SetCursorPos($point[0], $point[1])
                    Start-Sleep -Milliseconds 200
                    [HarborSafety.Native]::Click($true)
                    $open = (Wait-Until { [HarborSafety.Native]::WindowOf([uint32]$dock.Id, '#32768') -ne [IntPtr]::Zero } 3000) -ge 0
                    [HarborSafety.Native]::PressKeys(0x11, 0x12, 0x10, 0x48) # Ctrl, Alt, Shift, H
                    $ms = Wait-Until { Test-Restored } 5000
                    $closed = (Wait-Until { [HarborSafety.Native]::WindowOf([uint32]$dock.Id, '#32768') -eq [IntPtr]::Zero } 3000) -ge 0
                    $record = Get-RecordNote
                    Add-Result $name ($open -and $ms -ge 0 -and $closed -and $record.Gone) "menu opened: $open; restored in $ms ms; menu closed: $closed$($record.Text)"
                }
                'island-move' {
                    $monitors = [HarborSafety.Native]::Monitors()
                    if ($monitors.Count -lt 2) { Add-Result $name $true 'one monitor' -Skipped; break }
                    $start = Get-MonitorOf ([HarborSafety.Native]::RectOf((Get-Island $dock)))
                    $other = $monitors | Where-Object { $_[0] -ne $start[0] -or $_[1] -ne $start[1] } | Select-Object -First 1
                    $restoresBefore = Get-DockLogCount 'restoring because'
                    $movesBefore = Get-DockLogCount 'island: moves to monitor'
                    Invoke-EdgeTrigger $other
                    $there = Wait-Until { $m = Get-MonitorOf ([HarborSafety.Native]::RectOf((Get-Island $dock))); $null -ne $m -and $m[0] -eq $other[0] -and $m[1] -eq $other[1] } 3000
                    Invoke-EdgeTrigger $start
                    $back = Wait-Until { $m = Get-MonitorOf ([HarborSafety.Native]::RectOf((Get-Island $dock))); $null -ne $m -and $m[0] -eq $start[0] -and $m[1] -eq $start[1] } 3000
                    [void][HarborSafety.Native]::SetCursorPos([int](($start[0] + $start[2]) / 2), [int](($start[1] + $start[3]) / 2))
                    $moves = (Get-DockLogCount 'island: moves to monitor') - $movesBefore
                    $restores = (Get-DockLogCount 'restoring because') - $restoresBefore
                    $hidden = [HarborSafety.Native]::PrimaryHidden()
                    Add-Result $name ($there -ge 0 -and $back -ge 0 -and $restores -eq 0 -and $hidden) "there in $there ms, back in $back ms ($moves moves); taskbar restores meanwhile: $restores; still hidden: $hidden"
                }
                'budget' {
                    Start-Sleep -Seconds 5
                    $rows = New-Object System.Collections.Generic.List[string]
                    $b = Get-Budget $dock; $rows.Add("0 apps: $($b.Text)"); $max = $b.WorkingSet
                    $helper = Start-TestWindows
                    try {
                        for ($i = 1; $i -le 30; $i++) { Send-TestWindows $helper "new $i Harbor.Safety.App$i" }
                        Start-Sleep -Seconds 5
                        $b = Get-Budget $dock; $rows.Add("30 apps: $($b.Text)"); $max = [Math]::Max($max, $b.WorkingSet)
                        $dock.Refresh(); $cpuBefore = $dock.TotalProcessorTime
                        Start-Sleep -Seconds $IdleSeconds
                        $dock.Refresh()
                        $cpuPercent = ($dock.TotalProcessorTime - $cpuBefore).TotalMilliseconds / ($IdleSeconds * 1000 * [Environment]::ProcessorCount) * 100
                        $rows.Add(("30 apps at rest: {0:N3} % CPU over {1} s" -f $cpuPercent, $IdleSeconds))

                        # A label (resting on an item), then a menu (right-click, Esc).
                        $point = Get-IslandPoint $dock
                        if ([HarborSafety.Native]::ProcessAt($point[0], $point[1]) -ne [uint32]$dock.Id) { throw 'The island is covered.' }
                        [void][HarborSafety.Native]::SetCursorPos($point[0], $point[1])
                        Start-Sleep -Milliseconds 1500
                        [HarborSafety.Native]::Click($true)
                        Start-Sleep -Milliseconds 1000
                        [HarborSafety.Native]::PressKeys(0x1B) # Esc
                        $island = [HarborSafety.Native]::RectOf((Get-Island $dock))
                        [void][HarborSafety.Native]::SetCursorPos($point[0], $island[1] - 200)
                        Start-Sleep -Seconds 2
                        $b = Get-Budget $dock; $rows.Add("after a label and a menu: $($b.Text)"); $max = [Math]::Max($max, $b.WorkingSet)

                        $monitors = [HarborSafety.Native]::Monitors()
                        if ($monitors.Count -ge 2) {
                            $start = Get-MonitorOf $island
                            $other = $monitors | Where-Object { $_[0] -ne $start[0] -or $_[1] -ne $start[1] } | Select-Object -First 1
                            for ($i = 0; $i -lt 25; $i++) { Invoke-EdgeTrigger $other; Invoke-EdgeTrigger $start }
                            [void][HarborSafety.Native]::SetCursorPos([int](($start[0] + $start[2]) / 2), [int](($start[1] + $start[3]) / 2))
                            Start-Sleep -Seconds 2
                            $b = Get-Budget $dock; $rows.Add("after 50 moves: $($b.Text)"); $max = [Math]::Max($max, $b.WorkingSet)
                        }
                        else { $rows.Add('50 moves: skipped (one monitor)') }

                        # Windows of an app already there: the tracker's bookkeeping, not the island's.
                        for ($i = 0; $i -lt 10000; $i++) { Send-TestWindows $helper 'new 100 Harbor.Safety.App1'; Send-TestWindows $helper 'close 100' }
                        Start-Sleep -Seconds 5
                        $b = Get-Budget $dock; $rows.Add("after 10,000 windows came and went: $($b.Text)"); $max = [Math]::Max($max, $b.WorkingSet)
                    }
                    finally { Stop-TestWindows $helper }
                    Add-Result $name ($max -lt 40 -and $cpuPercent -lt 0.5) ($rows -join '; ')
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
