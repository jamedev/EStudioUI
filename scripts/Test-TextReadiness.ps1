<#
.SYNOPSIS
    Build/test the framework, profile text, and open the focused native text acceptance window.
.DESCRIPTION
    Creates an owning JSON acceptance record with every human observation initially
    NotTested. Closing the window never changes these results to Passed. Edit each
    result to Passed or Failed only after performing the documented steps.
    Uses the pinned x64-windows-estudioui build through Build.ps1 by default.
.PARAMETER PrepareOnly
    Write the acceptance scaffold without profiling or opening a window; suitable for CI.
.PARAMETER SkipProfile
    Open native acceptance without running the deterministic text profile first.
.EXAMPLE
    .\scripts\Test-TextReadiness.ps1 -SkipBuild
#>
#requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$PrepareOnly,
    [switch]$SkipProfile,
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
function Get-ProjectPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $projectRoot $Path))
}
$BuildDirectory = Get-ProjectPath $BuildDirectory
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot ('benchmark-local/text-readiness-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = Get-ProjectPath $OutputDirectory
if (-not $SkipBuild) {
    $buildArguments = @{ Configuration = 'Release'; BuildDirectory = $BuildDirectory }
    if ($VcpkgRoot) { $buildArguments.VcpkgRoot = $VcpkgRoot }
    & (Join-Path $projectRoot 'Build.ps1') @buildArguments
}
if (-not $ExecutablePath) { $ExecutablePath = Join-Path $BuildDirectory 'Release/EStudioUIDemo.exe' }
$ExecutablePath = Get-ProjectPath $ExecutablePath
if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
    throw "Release executable missing: $ExecutablePath. Run without -SkipBuild."
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$reportPath = Join-Path $OutputDirectory 'acceptance.json'
if (Test-Path -LiteralPath $reportPath) {
    throw "Acceptance record already exists: $reportPath. Choose a new output directory to preserve observations."
}
$checks = @(
    @('ime-japanese', 'Japanese IME: compose, change candidates, commit and cancel. Candidate window follows the caret; preedit does not alter canonical text before commit.'),
    @('ime-chinese', 'Chinese IME: repeat composition, candidate selection, commit, cancel and focus changes.'),
    @('ime-korean', 'Korean IME: compose syllables, backspace, commit and move focus.'),
    @('dead-keys-altgr', 'Use an international keyboard: dead-key accent plus letter, cancellation, and AltGr characters. No duplicate characters or unintended shortcuts.'),
    @('mixed-selection', 'Drag both directions across the Latin/Hebrew/Arabic field; release outside, Shift+arrows and Home/End. Clipboard text matches the logical selection.'),
    @('clipboard-undo', 'Copy/paste Unicode with another native application. Cut, undo and redo preserve exact text and selection; password data is not used in this harness.'),
    @('focus-composition', 'Switch fields and applications during preedit, then return. No stale commits or misplaced preedit/tooltip.'),
    @('dpi-candidates', 'Move between monitors with different scaling while editing and composing. Caret, selection, hit tests and candidate window follow the current monitor.'),
    @('narrator-text', 'Use Windows Narrator: names, value, selection, focus and edits are announced in each field without duplicate or stale text.'),
    @('font-preview', 'Change registered family, size, weight and italic. Preview shows actual font changes with mixed scripts and the configured RTL/language hints.'),
    @('hardware-profile', 'Run on the target physical GPU with driver/monitor/power details. Review text-profile.csv and existing Profile-Rendering.ps1 text/effects workloads; record observed stalls and timing coverage.')
)
$source = 'Unavailable'
$dirty = $null
if (Get-Command git -ErrorAction SilentlyContinue) {
    $revision = & git -C $projectRoot rev-parse HEAD 2>$null
    if ($LASTEXITCODE -eq 0) { $source = ($revision -join '').Trim() }
    $changes = & git -C $projectRoot status --porcelain --untracked-files=no 2>$null
    if ($LASTEXITCODE -eq 0) { $dirty = [bool]$changes }
}
$adapters = @()
try {
    $adapters = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, VideoModeDescription)
} catch { $adapters = @(@{ DiscoveryError = $_.Exception.Message }) }
$record = [ordered]@{
    SchemaVersion = 1
    CapturedAt = (Get-Date).ToString('o')
    SourceCommit = $source
    TrackedSourceDirty = $dirty
    Executable = $ExecutablePath
    ExecutableSha256 = (Get-FileHash -LiteralPath $ExecutablePath -Algorithm SHA256).Hash
    OS = [Environment]::OSVersion.VersionString
    PowerShell = $PSVersionTable.PSVersion.ToString()
    VideoControllers = $adapters
    MonitorAndScaling = 'Record physical monitors, scaling, refresh rates and which monitor hosts the window.'
    InputMethodsAndKeyboardLayouts = 'Record installed IMEs and keyboard layouts used for each observed result.'
    AutomatedTextProfile = 'NotRun'
    HumanChecks = @($checks | ForEach-Object { [ordered]@{ Id = $_[0]; Status = 'NotTested'; Steps = $_[1]; Notes = '' } })
}
function Save-Record { $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8 }
Save-Record
Write-Host "Acceptance record: $reportPath"
if ($PrepareOnly) { return }
if (-not $SkipProfile) {
    $record.AutomatedTextProfile = 'Running'
    Save-Record
    & $ExecutablePath --text-profile --output $OutputDirectory
    if ($LASTEXITCODE -ne 0) {
        $record.AutomatedTextProfile = 'Failed'
        Save-Record
        throw "Text profile failed with exit code $LASTEXITCODE."
    }
    $record.AutomatedTextProfile = 'Completed; timing interpretation requires the recorded driver and target hardware.'
    Save-Record
}
Write-Host 'Perform the native checks, then record observations in acceptance.json. Untested cases stay NotTested.'
& $ExecutablePath --text-readiness
if ($LASTEXITCODE -ne 0) { throw "Native text acceptance window failed with exit code $LASTEXITCODE." }
