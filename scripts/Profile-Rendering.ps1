<#
.SYNOPSIS
    Build/test Release, then profile native rendering without pixel readbacks.
.DESCRIPTION
    Uses Build.ps1 and its pinned x64-windows-estudioui dependencies by default.
    Runs the actual Rich CSS and synchronized Widget styles slider, an isolated
    effects/flat slider pair, and dense mixed-script TextBox edit/selection cases.
    Each case compares full and retained redraw at a scripted 60 Hz by default.
    Keep the window visible on one monitor and avoid unrelated GPU workloads.
    Reports record actual sizes, DPI, driver, frame counters and timing coverage.
    GPU scope timings are elapsed command-stream intervals, not utilization percentages.
.PARAMETER SkipBuild
    Use an already-built Release executable instead of rebuilding and testing first.
.PARAMETER Case
    Select one workload group or all groups.
.PARAMETER Frames
    Measured frames for each workload and redraw mode.
.PARAMETER Warmup
    Frames excluded from measured percentiles before each run.
.PARAMETER FrameRate
    Scripted rate; zero explicitly selects unpaced throughput.
.PARAMETER VcpkgRoot
    Optional preferred vcpkg checkout, forwarded to Build.ps1.
.PARAMETER BuildDirectory
    Repository-relative or absolute CMake build directory.
.PARAMETER ExecutablePath
    Optional native executable override, useful for an isolated CI graphics driver.
.PARAMETER OutputDirectory
    Repository-relative or absolute report directory; defaults to a timestamped folder.
.EXAMPLE
    .\scripts\Profile-Rendering.ps1
.EXAMPLE
    .\scripts\Profile-Rendering.ps1 -SkipBuild -Case widget-styles -Frames 600
#>

#requires -Version 5.1

[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateSet('all', 'rich-css', 'widget-styles', 'isolated-slider', 'text', 'baseline')]
    [string]$Case = 'all',
    [ValidateRange(1, 1800)]
    [int]$Frames = 180,
    [ValidateRange(0, 600)]
    [int]$Warmup = 30,
    [ValidateRange(0, 240)]
    [Alias('Rate')]
    [double]$FrameRate = 60,
    [string]$VcpkgRoot = '',
    [ValidateNotNullOrEmpty()]
    [string]$BuildDirectory = 'build/windows-vcpkg',
    [string]$ExecutablePath = '',
    [string]$OutputDirectory = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$projectRoot = Split-Path -Parent $PSScriptRoot

if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $projectRoot $BuildDirectory
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot ('benchmark-local/render-profile-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot $OutputDirectory
}

if (-not $SkipBuild) {
    $buildArguments = @{
        Configuration = 'Release'
        BuildDirectory = $BuildDirectory
    }
    if ($VcpkgRoot) { $buildArguments.VcpkgRoot = $VcpkgRoot }
    & (Join-Path $projectRoot 'Build.ps1') @buildArguments
}

if (-not $ExecutablePath) {
    $ExecutablePath = Join-Path $BuildDirectory 'Release/EStudioUIDemo.exe'
} elseif (-not [System.IO.Path]::IsPathRooted($ExecutablePath)) {
    $ExecutablePath = Join-Path $projectRoot $ExecutablePath
}
$executable = $ExecutablePath
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Release executable missing: $executable. Run without -SkipBuild."
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$session = [ordered]@{
    CapturedAt = (Get-Date).ToString('o')
    Executable = $executable
    ExecutableModifiedAt = (Get-Item -LiteralPath $executable).LastWriteTime.ToString('o')
    ScriptedRate = $FrameRate
    Case = $Case
    Frames = $Frames
    Warmup = $Warmup
    SkipBuild = [bool]$SkipBuild
}
if (Get-Command git -ErrorAction SilentlyContinue) {
    $commit = & git -C $projectRoot rev-parse HEAD 2>$null
    if ($LASTEXITCODE -eq 0) { $session.Commit = ($commit -join '').Trim() }
    $changes = & git -C $projectRoot status --porcelain 2>$null
    if ($LASTEXITCODE -eq 0) { $session.WorkingTreeDirty = [bool]$changes }
}
try {
    $session.VideoControllers = @(Get-CimInstance Win32_VideoController |
        Select-Object Name, DriverVersion, VideoProcessor, CurrentHorizontalResolution, CurrentVerticalResolution)
} catch {
    $session.VideoControllerQuery = 'Unavailable; OpenGL renderer metadata is still recorded.'
}
$session | ConvertTo-Json -Depth 4 | Out-File -LiteralPath (Join-Path $OutputDirectory 'render-profile-session.json') -Encoding utf8

Write-Host ''
Write-Host '== Profile native rendering ==' -ForegroundColor Cyan
Write-Host 'Keep the profile window visible; do not resize it or move it between monitors during capture.'
$rateArgument = $FrameRate.ToString([System.Globalization.CultureInfo]::InvariantCulture)
& $executable --profile-rendering --profile-case $Case --profile-frames $Frames --profile-warmup $Warmup --profile-rate $rateArgument --output $OutputDirectory
if ($LASTEXITCODE -ne 0) {
    throw "Native rendering profile failed with exit code $LASTEXITCODE."
}
Write-Host ''
Write-Host "Rendering reports: $OutputDirectory" -ForegroundColor Green
Get-Content -LiteralPath (Join-Path $OutputDirectory 'render-profile.txt')
