param(
    [Parameter(Mandatory=$true)]
    [Alias("Input")]
    [string]$InputPath,
    [string]$Background = "",
    [string]$LegacyAssets = "",
    [string]$Output = "",
    [switch]$NoSetup
)

$ErrorActionPreference = "Stop"
$StateRoot = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA "TouchPlus\MediaPipeBenchmark" } else { Join-Path $env:TEMP "TouchPlus-MediaPipeBenchmark" }
$Venv = Join-Path $StateRoot "venv"
$Python = Join-Path $Venv "Scripts\python.exe"
$MediaPipeModel = Join-Path $StateRoot "assets\models\hand_landmarker.task"
$Probe = Join-Path $PSScriptRoot "touchplus_mediapipe_refiner_benchmark.py"

$ResolvedInput = (Resolve-Path $InputPath).Path
$DatasetDir = Split-Path -Parent $ResolvedInput
$TouchPlusRoot = Split-Path -Parent $DatasetDir

if (-not $Background) {
    $Background = Join-Path $TouchPlusRoot "landmark-guided-captures\raw\pair-001-left.png"
}
if (-not $LegacyAssets) {
    $LegacyAssets = Join-Path $TouchPlusRoot "landmark-assets"
}
if (-not $Output) {
    $Output = Join-Path $StateRoot "output-refiner-m2"
}

if ((-not (Test-Path $Python)) -or (-not (Test-Path $MediaPipeModel))) {
    if ($NoSetup) {
        throw "MediaPipe benchmark environment is missing and -NoSetup was requested."
    }
    & (Join-Path $PSScriptRoot "setup-touchplus-mediapipe-benchmark.ps1")
    if ($LASTEXITCODE -ne 0) { throw "MediaPipe benchmark setup failed" }
}
if (-not (Test-Path $Background -PathType Leaf)) {
    throw "Background image not found: $Background`nPass -Background <no-hand LEFT image>."
}
if (-not (Test-Path $LegacyAssets -PathType Container)) {
    throw "Legacy landmark assets not found: $LegacyAssets`nExpected the existing 2B.9B.1 landmark-assets directory."
}

& $Python $Probe `
    --input $ResolvedInput `
    --background $Background `
    --legacy-assets $LegacyAssets `
    --mediapipe-model $MediaPipeModel `
    --output $Output

if ($LASTEXITCODE -ne 0) { throw "2B.10M.2 refiner benchmark failed with exit code $LASTEXITCODE" }

Write-Host ""
Write-Host "2B.10M.2 refiner benchmark complete."
Write-Host "Overlays: $Output\annotations"
Write-Host "Summary:  $Output\summary.json"
Write-Host "CSV:      $Output\summary.csv"
