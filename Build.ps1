<#
.SYNOPSIS
    Install EStudioUI's vcpkg dependencies, build with VS 2022, and run tests.
.DESCRIPTION
    Uses the repository's x64-windows-estudioui overlay triplet and pinned
    manifest. Dependencies are installed in the build directory, so a shared
    vcpkg checkout can be reused without changing other projects' installed trees.
    When vcpkg is unavailable, a private checkout is bootstrapped under .tools.
.PARAMETER Configuration
    Debug or Release. Both dependency configurations are available in the triplet.
.PARAMETER VcpkgRoot
    Optional existing vcpkg Git checkout, or an empty path where it can be cloned.
.PARAMETER BuildDirectory
    Optional output directory, relative to the repository or an absolute path.
.PARAMETER Jobs
    Maximum parallel compiler jobs for vcpkg and the application build.
.PARAMETER SkipGpuTest
    Skip the native OpenGL self-test on machines without an OpenGL 3.3 context.
    Core tests still run. Intended for hosted Windows CI runners.
.PARAMETER Run
    Open the interactive demo after the automated checks pass.
.EXAMPLE
    .\Build.ps1 -VcpkgRoot C:\Development\vcpkg -Run
#>

#requires -Version 5.1

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$VcpkgRoot = '',
    [ValidateNotNullOrEmpty()]
    [string]$BuildDirectory = 'build/windows-vcpkg',
    [ValidateRange(1, 128)]
    [int]$Jobs = [Math]::Max(1, [Math]::Min(4, [Environment]::ProcessorCount)),
    [switch]$SkipGpuTest,
    [switch]$Run
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# PS 7.3+ can otherwise throw before we can attach the failing step/exit code.
$PSNativeCommandUseErrorActionPreference = $false
$projectRoot = $PSScriptRoot
$targetTriplet = 'x64-windows-estudioui'
$script:currentStep = 'preflight'

function Invoke-Checked
{
    param([string]$Step, [string]$FilePath, [string[]]$ArgumentList = @())
    $script:currentStep = $Step
    Write-Host "`n== $Step ==" -ForegroundColor Cyan
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0)
    {
        throw "$Step failed with exit code $LASTEXITCODE."
    }
}

function Get-ApplicationPath
{
    param([string]$Name)
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command)
    {
        return $command.Source
    }
    return $null
}

function Get-ProjectPath
{
    param([string]$Path)
    if ([IO.Path]::IsPathRooted($Path))
    {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $projectRoot $Path))
}

function Find-VisualStudio
{
    $vswhere = Get-ApplicationPath 'vswhere.exe'
    if (-not $vswhere -and ${env:ProgramFiles(x86)})
    {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    }
    if (-not $vswhere -or -not (Test-Path -LiteralPath $vswhere -PathType Leaf))
    {
        throw 'Install Visual Studio 2022 with Desktop development with C++, a Windows SDK, and CMake tools.'
    }
    $json = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json -utf8
    if ($LASTEXITCODE -ne 0)
    {
        throw "vswhere failed with exit code $LASTEXITCODE."
    }
    $installations = @((($json -join "`n") | ConvertFrom-Json))
    if ($installations.Count -eq 0 -or -not $installations[0])
    {
        throw 'Visual Studio 2022 C++ x64 tools were not found. Enable the Desktop development with C++ workload.'
    }
    $installation = $installations[0].installationPath
    if (-not (Test-Path -LiteralPath (Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat')))
    {
        throw "The VS 2022 installation has no x64 C++ environment: $installation"
    }
    return $installation
}

function Find-CMake
{
    param([string]$VisualStudio)
    $candidates = @(
        (Get-ApplicationPath 'cmake.exe'),
        (Join-Path $VisualStudio 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
    )
    foreach ($candidate in $candidates)
    {
        if (-not $candidate -or -not (Test-Path -LiteralPath $candidate -PathType Leaf))
        {
            continue
        }
        $description = (& $candidate --version) -join "`n"
        if ($LASTEXITCODE -eq 0 -and $description -match 'cmake version (\d+\.\d+\.\d+)')
        {
            if ([version]$Matches[1] -ge [version]'3.24.0')
            {
                return $candidate
            }
        }
    }
    throw 'CMake 3.24 or newer was not found. Enable C++ CMake tools in Visual Studio Installer or add CMake to PATH.'
}

function Test-VcpkgCheckout
{
    param([string]$Path)
    return $Path -and (Test-Path -LiteralPath (Join-Path $Path 'bootstrap-vcpkg.bat')) -and
        (Test-Path -LiteralPath (Join-Path $Path '.git'))
}

function Find-VcpkgRoot
{
    if ($VcpkgRoot)
    {
        return Get-ProjectPath $VcpkgRoot
    }
    if ($env:VCPKG_ROOT)
    {
        $candidate = Get-ProjectPath $env:VCPKG_ROOT
        if (Test-VcpkgCheckout $candidate)
        {
            return $candidate
        }
    }
    $vcpkgCommand = Get-ApplicationPath 'vcpkg.exe'
    if ($vcpkgCommand)
    {
        $candidate = Split-Path -Parent $vcpkgCommand
        if (Test-VcpkgCheckout $candidate)
        {
            return $candidate
        }
    }
    # Also finds layouts such as C:\Development\Projects\EStudioUI and
    # C:\Development\vcpkg, without hard-coding a machine-specific path.
    $ancestor = Split-Path -Parent $projectRoot
    while ($ancestor)
    {
        $candidate = Join-Path $ancestor 'vcpkg'
        if (Test-VcpkgCheckout $candidate)
        {
            return $candidate
        }
        $ancestor = Split-Path -Parent $ancestor
    }
    return Join-Path $projectRoot '.tools/vcpkg'
}

$oldVcpkgRoot = $env:VCPKG_ROOT
$oldConcurrency = $env:VCPKG_MAX_CONCURRENCY
try
{
    if ($env:OS -ne 'Windows_NT')
    {
        throw 'Build.ps1 targets Windows/MSVC. On Linux, use scripts/Build-Linux.sh.'
    }
    $visualStudio = Find-VisualStudio
    $cmake = Find-CMake $visualStudio
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $ctest -PathType Leaf))
    {
        throw "ctest.exe was not found beside CMake: $cmake"
    }
    $git = Get-ApplicationPath 'git.exe'
    if (-not $git)
    {
        $git = Join-Path $visualStudio 'Common7/IDE/CommonExtensions/Microsoft/TeamFoundation/Team Explorer/Git/cmd/git.exe'
    }
    if (-not (Test-Path -LiteralPath $git -PathType Leaf))
    {
        throw 'Git for Windows was not found. Install it or enable the Visual Studio Git command-line tools.'
    }
    $manifest = Get-Content -LiteralPath (Join-Path $projectRoot 'vcpkg.json') -Raw | ConvertFrom-Json
    $baseline = $manifest.'builtin-baseline'
    $vcpkgDirectory = Find-VcpkgRoot
    $buildRoot = Get-ProjectPath $BuildDirectory
    $installedRoot = Join-Path $buildRoot 'vcpkg_installed'
    New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null

    if (-not (Test-VcpkgCheckout $vcpkgDirectory))
    {
        if ((Test-Path -LiteralPath $vcpkgDirectory) -and
            (-not (Test-Path -LiteralPath $vcpkgDirectory -PathType Container) -or
             @(Get-ChildItem -LiteralPath $vcpkgDirectory -Force).Count -gt 0))
        {
            throw "The vcpkg path is not an empty directory or a vcpkg Git checkout: $vcpkgDirectory"
        }
        New-Item -ItemType Directory -Path (Split-Path -Parent $vcpkgDirectory) -Force | Out-Null
        Invoke-Checked 'Clone vcpkg' $git @('clone', '--branch', '2025.04.09', '--single-branch', 'https://github.com/microsoft/vcpkg.git', $vcpkgDirectory)
    }
    # Fetching a missing historical object does not change an existing checkout's HEAD.
    $knownBaseline = & $git -C $vcpkgDirectory rev-parse --verify --quiet "${baseline}^{commit}"
    if ($LASTEXITCODE -ne 0)
    {
        Invoke-Checked 'Fetch pinned registry baseline' $git @('-C', $vcpkgDirectory, 'fetch', '--no-tags', 'origin', $baseline)
    }
    $env:VCPKG_ROOT = $vcpkgDirectory
    $env:VCPKG_MAX_CONCURRENCY = $Jobs.ToString()
    $vcpkg = Join-Path $vcpkgDirectory 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf))
    {
        Invoke-Checked 'Bootstrap vcpkg' (Join-Path $vcpkgDirectory 'bootstrap-vcpkg.bat') @('-disableMetrics')
    }
    if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf))
    {
        throw "Bootstrap did not create $vcpkg"
    }

    Write-Host "VS 2022: $visualStudio"
    Write-Host "vcpkg:   $vcpkgDirectory"
    Write-Host "Triplet: $targetTriplet (static libraries, dynamic MSVC runtime)"
    Write-Host "Install: $installedRoot"
    Invoke-Checked 'Install dependencies' $vcpkg @(
        'install', "--triplet=$targetTriplet", '--host-triplet=x64-windows',
        "--overlay-triplets=$(Join-Path $projectRoot 'cmake/triplets')",
        "--x-manifest-root=$projectRoot", "--x-install-root=$installedRoot", '--disable-metrics'
    )
    Invoke-Checked 'Configure EStudioUI' $cmake @(
        '-S', $projectRoot, '-B', $buildRoot, '-G', 'Visual Studio 17 2022', '-A', 'x64',
        "-DCMAKE_GENERATOR_INSTANCE:PATH=$visualStudio",
        "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=$(Join-Path $vcpkgDirectory 'scripts/buildsystems/vcpkg.cmake')",
        '-DVCPKG_MANIFEST_MODE:BOOL=ON', '-DVCPKG_MANIFEST_INSTALL:BOOL=OFF',
        "-DVCPKG_TARGET_TRIPLET:STRING=$targetTriplet", '-DVCPKG_HOST_TRIPLET:STRING=x64-windows',
        "-DVCPKG_INSTALLED_DIR:PATH=$installedRoot",
        "-DVCPKG_OVERLAY_TRIPLETS:PATH=$(Join-Path $projectRoot 'cmake/triplets')",
        '-DCMAKE_MSVC_RUNTIME_LIBRARY:STRING=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL',
        '-DESTUDIOUI_BUILD_DEMO:BOOL=ON', '-DESTUDIOUI_BUILD_TESTS:BOOL=ON', '-DESTUDIOUI_GPU_TESTS:BOOL=OFF'
    )
    Invoke-Checked 'Build EStudioUI' $cmake @('--build', $buildRoot, '--config', $Configuration, '--parallel', $Jobs.ToString())
    Invoke-Checked 'Run core tests' $ctest @('--test-dir', $buildRoot, '-C', $Configuration, '--output-on-failure', '--no-tests=error')
    $executable = Join-Path $buildRoot "$Configuration/EStudioUIDemo.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf))
    {
        throw "The demo executable was not produced: $executable"
    }
    if (-not $SkipGpuTest)
    {
        $verification = Join-Path $buildRoot "verification/$Configuration"
        Invoke-Checked 'Run native GPU self-test' $executable @('--self-test', '--output', $verification)
    }
    else
    {
        Write-Host 'Native GPU self-test skipped by -SkipGpuTest; core tests passed.'
    }
    Write-Host "`nEStudioUI build and selected checks passed: $executable" -ForegroundColor Green
    if ($Run)
    {
        Invoke-Checked 'Launch interactive demo' $executable
    }
}
catch
{
    Write-Host "`nEStudioUI failed during $script:currentStep`: $($_.Exception.Message)" -ForegroundColor Red
    throw
}
finally
{
    [Environment]::SetEnvironmentVariable('VCPKG_ROOT', $oldVcpkgRoot, 'Process')
    [Environment]::SetEnvironmentVariable('VCPKG_MAX_CONCURRENCY', $oldConcurrency, 'Process')
}
