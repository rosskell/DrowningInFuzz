param(
    [ValidateSet("Debug", "Release")]
    [string] $Config = "Release",

    [string] $InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\Common\VST3"),

    [switch] $SkipInstall,
    [switch] $SkipDist,
    [switch] $CleanOldProFuzz
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $repoRoot "build"
$distDir = Join-Path $repoRoot "dist"
$pluginName = "Drowning in Fuzz.vst3"
$artifact = Join-Path $buildDir "DrowningInFuzz_artefacts\$Config\VST3\$pluginName"
$distPlugin = Join-Path $distDir $pluginName

function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $vsRoots = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Professional",
        "C:\Program Files\Microsoft Visual Studio\2022\Community",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
    )

    foreach ($root in $vsRoots) {
        $candidate = Join-Path $root "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $candidate) { return $candidate }
    }

    throw "CMake was not found on PATH or in the usual Visual Studio 2022 locations."
}

function Find-VcVars {
    $vsRoots = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Professional",
        "C:\Program Files\Microsoft Visual Studio\2022\Community",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
    )

    foreach ($root in $vsRoots) {
        $candidate = Join-Path $root "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $candidate) { return $candidate }
    }

    return $null
}

function Normalize-PathEnvironment {
    $pathValue = [Environment]::GetEnvironmentVariable("Path", "Process")
    if (-not $pathValue) {
        $pathValue = [Environment]::GetEnvironmentVariable("PATH", "Process")
    }

    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    $env:Path = $pathValue
}

function Invoke-BuildCommand {
    param([string] $Command)

    if ($script:vcVars) {
        & cmd.exe /d /c "`"$script:vcVars`" && $Command"
    } else {
        & cmd.exe /d /c $Command
    }

    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE"
    }
}

Push-Location $repoRoot
try {
    Normalize-PathEnvironment

    $cmake = Find-CMake
    $script:vcVars = Find-VcVars

    Write-Host "Configuring Drowning in Fuzz..."
    Invoke-BuildCommand "`"$cmake`" -B `"$buildDir`" -G `"Visual Studio 17 2022`" -A x64"

    Write-Host "Building $Config..."
    Invoke-BuildCommand "`"$cmake`" --build `"$buildDir`" --config $Config"

    if (-not (Test-Path $artifact)) {
        throw "Built VST3 was not found at $artifact"
    }

    if (-not $SkipDist) {
        New-Item -ItemType Directory -Path $distDir -Force | Out-Null
        Copy-Item -Path $artifact -Destination $distDir -Recurse -Force
        Write-Host "Updated dist package: $distPlugin"
    }

    if (-not $SkipInstall) {
        New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null

        if ($CleanOldProFuzz) {
            $oldPlugin = Join-Path $InstallDir "ProFuzz.vst3"
            if (Test-Path $oldPlugin) {
                Remove-Item -LiteralPath $oldPlugin -Recurse -Force
                Write-Host "Removed old plugin: $oldPlugin"
            }
        }

        Copy-Item -Path $artifact -Destination $InstallDir -Recurse -Force
        Write-Host "Installed VST3: $(Join-Path $InstallDir $pluginName)"
    }
} finally {
    Pop-Location
}
