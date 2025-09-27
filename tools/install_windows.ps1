<#
  ==============================================================================

   This file is part of the YUP library.
   Copyright (c) 2025 - kunitoki@gmail.com

   YUP is an open source library subject to open-source licensing.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   to use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   YUP IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
#>

<#!
.SYNOPSIS
    Automated Windows setup for the YUP Rive → NDI toolchain.

.DESCRIPTION
    Creates an isolated Python environment, installs build requirements, configures
    and builds the native renderer with Visual Studio 2022, optionally builds the
    Python wheel, and executes smoke tests. Run this script from a "x64 Native Tools
    Command Prompt for VS 2022" or a PowerShell session where the MSVC toolchain is
    available on PATH.

.PARAMETER Configuration
    Multi-config build configuration to compile (Debug or Release). Defaults to Release.

.PARAMETER BuildDirectory
    Location of the CMake build tree. Defaults to "build" in the repository root.

.PARAMETER PythonVersion
    Python interpreter selector passed to `py -<version>`. Defaults to 3.11. Ignored when
    -PythonExecutable is provided.

.PARAMETER PythonExecutable
    Explicit path to the Python interpreter used to create the virtual environment. When
    omitted, the launcher `py` is invoked with -PythonVersion.

.PARAMETER VirtualEnvPath
    Path of the virtual environment to manage. Defaults to ".venv" in the repository root.

.PARAMETER SkipWheel
    Skip building and installing the Python wheel. Smoke tests are also skipped when this flag is set.

.PARAMETER SkipSmokeTests
    Skip the renderer/orchestrator smoke tests after wheel installation.

.PARAMETER InstallCyndilib
    Install the optional cyndilib runtime into the managed virtual environment.

.EXAMPLE
    .\tools\install_windows.ps1

    Perform a Release build, produce the Python wheel, reinstall it, and execute the smoke tests.

.EXAMPLE
    .\tools\install_windows.ps1 -Configuration Debug -SkipSmokeTests

    Build the Debug configuration but do not run the smoke tests after installing the wheel.

.NOTES
    This script must be executed on Windows and expects Visual Studio 2022 build tools to be available.
    When run from PowerShell 7+, ensure `&` is used to invoke the script (the default execution policy
    may need to be relaxed, for example `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass`).
#>

[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$BuildDirectory = "build",

    [string]$PythonVersion = "3.11",

    [string]$PythonExecutable,

    [string]$VirtualEnvPath,

    [switch]$SkipWheel,

    [switch]$SkipSmokeTests,

    [switch]$InstallCyndilib
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $IsWindows) {
    throw "tools/install_windows.ps1 must be run on Windows."
}

function Write-Section {
    param([string]$Message)
    Write-Host "`n=== $Message ===" -ForegroundColor Cyan
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

if (-not $VirtualEnvPath) {
    $VirtualEnvPath = Join-Path $repoRoot '.venv'
} else {
    $resolvedVenv = Resolve-Path -Path $VirtualEnvPath -ErrorAction SilentlyContinue
    if ($resolvedVenv) {
        $VirtualEnvPath = $resolvedVenv.Path
    }
}

if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $buildPath = $BuildDirectory
} else {
    $buildPath = Join-Path $repoRoot $BuildDirectory
}

if (-not (Test-Path $buildPath)) {
    Write-Section "Creating build directory at $buildPath"
    New-Item -ItemType Directory -Path $buildPath | Out-Null
}

if ($PythonExecutable) {
    if (-not (Test-Path $PythonExecutable)) {
        throw "Python executable not found at $PythonExecutable."
    }
    $pythonCommand = (Resolve-Path $PythonExecutable).Path
    $pythonBootstrapArgs = $null
} else {
    $pythonCommand = 'py'
    $pythonBootstrapArgs = if ($PythonVersion) { "-$PythonVersion" } else { $null }
}

if ([System.IO.Path]::IsPathRooted($VirtualEnvPath)) {
    $venvPath = $VirtualEnvPath
} else {
    $venvPath = Join-Path $repoRoot $VirtualEnvPath
}

$venvScripts = Join-Path $venvPath 'Scripts'
$venvPython = Join-Path $venvScripts 'python.exe'

Write-Section "Ensuring Python virtual environment at $venvPath"
if (-not (Test-Path $venvPython)) {
    $createVenvArgs = @('-m', 'venv', $venvPath)
    if ($pythonBootstrapArgs) {
        & $pythonCommand $pythonBootstrapArgs @createVenvArgs
    } else {
        & $pythonCommand @createVenvArgs
    }
} else {
    Write-Host "Virtual environment already exists." -ForegroundColor DarkGray
}

if (-not (Test-Path $venvPython)) {
    throw "Failed to create python virtual environment at $venvPath."
}

Write-Section "Upgrading pip and core build dependencies"
& $venvPython -m pip install --upgrade pip cmake ninja build pytest

if ($InstallCyndilib) {
    Write-Section "Installing optional cyndilib runtime"
    & $venvPython -m pip install cyndilib==0.0.8
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake is not available on PATH. Run from a VS 2022 developer prompt or install CMake."
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Warning "MSVC compiler (cl.exe) was not detected. Ensure you are running inside a Visual Studio developer prompt."
}

Write-Section "Configuring CMake project"
$cmakeConfigureArgs = @(
    '-S', $repoRoot,
    '-B', $buildPath,
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64',
    '-DYUP_ENABLE_AUDIO_MODULES=OFF',
    '-DYUP_BUILD_TESTS=ON',
    '-DYUP_BUILD_EXAMPLES=OFF'
)
cmake @cmakeConfigureArgs

Write-Section "Building $Configuration configuration"
$cmakeBuildArgs = @(
    '--build', $buildPath,
    '--config', $Configuration,
    '--target', 'ALL_BUILD'
)
cmake @cmakeBuildArgs

if ($SkipWheel) {
    Write-Host "Skipping wheel build and smoke tests as requested." -ForegroundColor Yellow
    return
}

$pythonDir = Join-Path $repoRoot 'python'
if (-not (Test-Path $pythonDir)) {
    throw "Python source directory not found at $pythonDir."
}

Write-Section "Building Python wheel"
Push-Location $pythonDir
try {
    $previousAudioSetting = $env:YUP_ENABLE_AUDIO_MODULES
    $env:YUP_ENABLE_AUDIO_MODULES = '0'
    try {
        & $venvPython -m build --wheel
    }
    finally {
        if ($null -eq $previousAudioSetting) {
            Remove-Item Env:YUP_ENABLE_AUDIO_MODULES -ErrorAction SilentlyContinue
        }
        else {
            $env:YUP_ENABLE_AUDIO_MODULES = $previousAudioSetting
        }
    }

    $distDir = Join-Path $pythonDir 'dist'
    $wheel = Get-ChildItem -Path $distDir -Filter 'yup-*.whl' |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if (-not $wheel) {
        throw "Wheel build did not produce a distributable in python/dist."
    }

    Write-Section "Installing freshly built wheel"
    & $venvPython -m pip install --force-reinstall $wheel.FullName

    if (-not $SkipSmokeTests) {
        Write-Section "Running renderer and NDI smoke tests"
        & $venvPython -m pytest -q `
            tests/test_yup_rive_renderer/test_binding_interface.py `
            tests/test_yup_ndi/test_orchestrator.py
    } else {
        Write-Host "Smoke tests skipped." -ForegroundColor Yellow
    }
}
finally {
    Pop-Location
}

Write-Section "Installation complete"
Write-Host "Virtual environment: $venvPath" -ForegroundColor Green
Write-Host "Build output: $buildPath" -ForegroundColor Green
