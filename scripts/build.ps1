<#
.SYNOPSIS
    Builds and tests Harbor for one target architecture.

.DESCRIPTION
    Enters the Visual Studio developer environment for the target architecture (the host
    architecture is detected), then builds and tests the C++ dock with CMake presets and the
    .NET solution with dotnet. Run it once per architecture. The caller's environment is restored afterwards, so x64 and
    arm64 runs can follow each other in the same shell.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Arch arm64
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('x64', 'arm64')]
    [string] $Arch,

    [ValidateSet('Debug', 'Release')]
    [string] $Config = 'Debug',

    # Build only. Required when the host can't run the target (arm64 binaries on an x64 machine).
    [switch] $NoTest
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot

function Invoke-Native {
    param([string] $FilePath, [string[]] $Arguments)
    Write-Host "> $FilePath $($Arguments -join ' ')" -ForegroundColor Cyan
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$FilePath $($Arguments -join ' ')' failed with exit code $LASTEXITCODE."
    }
}

# The machine's native architecture. $env:PROCESSOR_ARCHITECTURE would report the emulated
# architecture when this script runs in an x64 PowerShell on an ARM64 machine.
function Get-HostArch {
    $native = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment').PROCESSOR_ARCHITECTURE
    switch ($native) {
        'ARM64' { return 'arm64' }
        'AMD64' { return 'x64' }
        default { throw "Unsupported host architecture '$native'." }
    }
}

function Enter-VsDevEnvironment {
    param([string] $TargetArch, [string] $HostArch)

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe not found. Install Visual Studio or Build Tools (see README.md).'
    }

    $toolset = if ($TargetArch -eq 'arm64') { 'Microsoft.VisualStudio.Component.VC.Tools.ARM64' }
               else { 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' }
    $vsPath = & $vswhere -latest -products * -requires $toolset -property installationPath
    if (-not $vsPath) {
        throw "No Visual Studio installation has the $TargetArch MSVC toolset ($toolset). See README.md."
    }

    # VsDevCmd's helper scripts call vswhere.exe by name.
    $env:Path = "$(Split-Path $vswhere);$env:Path"

    $devShell = Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1'
    $vsArch = if ($TargetArch -eq 'x64') { 'amd64' } else { 'arm64' }
    $vsHostArch = if ($HostArch -eq 'x64') { 'amd64' } else { 'arm64' }

    # Visual Studio's script isn't written for strict mode.
    & {
        Set-StrictMode -Off
        & $devShell -Arch $vsArch -HostArch $vsHostArch -SkipAutomaticLocation -NoLogo
    }

    if (-not $env:VCPKG_ROOT -or -not (Test-Path (Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'))) {
        throw 'VCPKG_ROOT is not set to a vcpkg installation. Install the "vcpkg package manager" VS component or set VCPKG_ROOT (see README.md).'
    }
}

# Reads the target machine type from a PE file header.
function Get-PeArch {
    param([string] $Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    switch ($machine) {
        0x8664 { return 'x64' }
        0xAA64 { return 'arm64' }
        default { return ('0x{0:X4}' -f $machine) }
    }
}

if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw 'dotnet was not found on PATH. Install the .NET SDK pinned in global.json (see README.md), then open a new terminal.'
}

$hostArch = Get-HostArch
if (-not $NoTest -and $hostArch -eq 'x64' -and $Arch -eq 'arm64') {
    throw 'ARM64 binaries cannot run on an x64 machine. Re-run with -NoTest to build only; ARM64 tests run on ARM64 hardware.'
}

$savedEnvironment = @{}
Get-ChildItem Env: | ForEach-Object { $savedEnvironment[$_.Name] = $_.Value }
Push-Location $repoRoot
try {
    # English compiler output, whatever the Visual Studio UI language.
    $env:VSLANG = '1033'
    Enter-VsDevEnvironment -TargetArch $Arch -HostArch $hostArch

    $preset = "msvc-$Arch"
    Write-Host "== C++ $preset ($Config) on $hostArch host ==" -ForegroundColor Green
    Invoke-Native cmake @('--preset', $preset)
    Invoke-Native cmake @('--build', '--preset', "$preset-$($Config.ToLowerInvariant())")

    $dockExe = Join-Path $repoRoot "build\$preset\src\dock\$Config\harbor-dock.exe"
    $dockArch = Get-PeArch $dockExe
    if ($dockArch -ne $Arch) {
        throw "Expected an $Arch harbor-dock.exe but built $dockArch."
    }
    Write-Host "harbor-dock.exe is $dockArch" -ForegroundColor Green

    if (-not $NoTest) {
        Invoke-Native ctest @('--preset', $preset, '--build-config', $Config)
    }

    # Solution builds can't take a RuntimeIdentifier (NETSDK1134); Harbor.slnx maps each solution
    # platform to the projects, and Directory.Build.props maps the platform to a RuntimeIdentifier.
    $platform = if ($Arch -eq 'x64') { 'x64' } else { 'ARM64' }
    Write-Host "== .NET $platform ($Config) ==" -ForegroundColor Green
    Invoke-Native dotnet @('build', 'Harbor.slnx', '-c', $Config, "-p:Platform=$platform")
    if (-not $NoTest) {
        # The end-to-end test launches the dock built above, for the same architecture.
        $env:HARBOR_DOCK_EXE = $dockExe
        Invoke-Native dotnet @('test', '--solution', 'Harbor.slnx', '-c', $Config, "-p:Platform=$platform", '--no-build')
    }
}
finally {
    Pop-Location
    foreach ($name in @(Get-ChildItem Env: | ForEach-Object Name)) {
        if (-not $savedEnvironment.ContainsKey($name)) {
            [Environment]::SetEnvironmentVariable($name, $null)
        }
    }
    foreach ($entry in $savedEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value)
    }
}
