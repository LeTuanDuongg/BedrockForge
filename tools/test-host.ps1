param(
    [string]$Compiler = 'C:/msys64/ucrt64/bin/g++.exe',
    [string]$Python = 'C:/Users/letua/AppData/Local/Programs/Python/Python314/python.exe'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'build/sdk-gameplay-host'
New-Item -ItemType Directory -Force $output | Out-Null
$env:PATH = (Split-Path $Compiler -Parent) + ';' + $env:PATH
function Run([string]$program, [string[]]$arguments) {
    # unittest writes successful progress to stderr in Windows PowerShell 5.
    $ErrorActionPreference = 'Continue'
    & $program @arguments 2>&1 | ForEach-Object {
        if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message }
        else { $_.ToString() }
    }
    if ($LASTEXITCODE -ne 0) { throw "$program failed with exit code $LASTEXITCODE" }
}
function Test-Native([string]$name, [string[]]$sources) {
    $exe = Join-Path $output "$name.exe"
    $flags = @('-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', "$root/sdk", '-I', "$root/native")
    $files = @($sources | ForEach-Object { Join-Path $root $_ })
    Run $Compiler ($flags + $files + @('-o', $exe))
    Run $exe @()
}
Test-Native 'hybrid_sdk_tests' @('native/sdk_runtime.cpp', 'native/engine_bridge/block_read_126504.cpp', 'native/engine_bridge/world_read_126504.cpp', 'tests/sdk_runtime_tests.cpp')
Test-Native 'hybrid_sdk_block_api_tests' @('native/sdk_runtime.cpp', 'native/engine_bridge/world_read_126504.cpp', 'tests/sdk_block_api_tests.cpp')
Test-Native 'hybrid_sdk_world_api_tests' @('native/sdk_runtime.cpp', 'tests/sdk_world_api_tests.cpp')
Test-Native 'hybrid_self_tests' @('tests/sha_tests.cpp')
Test-Native 'iron_chest_core_tests' @('native/iron_chest_container.cpp', 'tests/iron_chest_core_tests.cpp')
Run $Python @('-m', 'unittest', 'discover', '-s', "$root/tests", '-p', 'test_*.py')
Write-Output 'PASS: five portable native host suites and Python tests; no ADB used. Android-only lifecycle/memory suites require device execution.'
