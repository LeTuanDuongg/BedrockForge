param(
    [Parameter(Mandatory = $true)][string]$InputTsv,
    [Parameter(Mandatory = $true)][int]$Offset,
    [Parameter(Mandatory = $true)][int]$Count,
    [string]$ProjectDir = "build\ghidra-1.26.50.4",
    [string]$ProjectName = "HybridMCPE_1_26_50_4",
    [string]$BackupDir = "build\ghidra-1.26.50.4-preincremental-2026-10-16",
    [string]$GhidraHome = "C:\Users\letua\Downloads\ghidra_12.1.4_PUBLIC_20260921\ghidra_12.1.4_PUBLIC"
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$projectPath = (Resolve-Path (Join-Path $repo $ProjectDir)).Path
$backupPath = (Resolve-Path (Join-Path $repo $BackupDir)).Path
if (-not (Test-Path -LiteralPath $backupPath)) { throw "Create project backup before import: $backupPath" }
if ($Count -lt 1 -or $Count -gt 64 -or $Offset -lt 0) { throw "Use Offset >= 0 and Count in 1..64" }
$inputPath = (Resolve-Path -LiteralPath $InputTsv).Path
$reportDir = Join-Path $repo "build\ghidra-fde-1.26.50.4\imports"
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$reportPath = Join-Path $reportDir "batch-$Offset-$Count-report.txt"
$logPath = Join-Path $reportDir "batch-$Offset-$Count-ghidra.log"
$headless = Join-Path $GhidraHome "support\analyzeHeadless.bat"
if (-not (Test-Path -LiteralPath $headless)) { throw "Ghidra headless not found: $headless" }
$oldJavaHome = $env:JAVA_HOME
$oldAppData = $env:APPDATA
$oldMaxMem = $env:GHIDRA_HEADLESS_MAXMEM
try {
    $env:JAVA_HOME = "C:\Program Files\Eclipse Adoptium\jdk-25.0.4.101-hotspot"
    $env:APPDATA = Join-Path $repo "build\ghidra-appdata"
    $env:GHIDRA_HEADLESS_MAXMEM = "4G"
    New-Item -ItemType Directory -Force -Path $env:APPDATA | Out-Null
    & $headless $projectPath $ProjectName -process "libminecraftpe.so" -noanalysis `
        -scriptPath (Join-Path $repo "tools\ghidra") -postScript ImportFdeBatch.java `
        $inputPath $Offset $Count $reportPath *> $logPath
    $exitCode = $LASTEXITCODE
} finally {
    $env:JAVA_HOME = $oldJavaHome
    $env:APPDATA = $oldAppData
    $env:GHIDRA_HEADLESS_MAXMEM = $oldMaxMem
}
Select-String -LiteralPath $logPath -Pattern "batch_result|ERROR|Exception|Save succeeded|FDE TSV" |
    Select-Object -Last 20 | ForEach-Object { $_.Line }
if ($exitCode -ne 0) { throw "Ghidra batch failed ($exitCode); full log: $logPath" }
if (-not (Test-Path -LiteralPath $reportPath)) { throw "Import report missing: $reportPath" }
Write-Output "report=$reportPath"
