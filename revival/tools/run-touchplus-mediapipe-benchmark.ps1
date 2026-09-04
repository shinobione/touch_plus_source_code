param(
    [Parameter(Mandatory=$true)]
    [Alias("Input")]
    [string]$InputPath,
    [string]$Output = "",
    [ValidateSet("left", "all")][string]$Eye = "left",
    [int]$NumHands = 2,
    [double]$MinDetection = 0.5,
    [double]$MinPresence = 0.5,
    [switch]$NoSetup
)

$ErrorActionPreference = "Stop"
$StateRoot = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA "TouchPlus\MediaPipeBenchmark" } else { Join-Path $env:TEMP "TouchPlus-MediaPipeBenchmark" }
$Venv = Join-Path $StateRoot "venv"
$Assets = Join-Path $StateRoot "assets"
if (-not $Output) { $Output = Join-Path $StateRoot "output" }
$Python = Join-Path $Venv "Scripts\python.exe"
$Model = Join-Path $Assets "models\hand_landmarker.task"
$Probe = Join-Path $PSScriptRoot "touchplus_mediapipe_benchmark.py"

if ((-not (Test-Path $Python)) -or (-not (Test-Path $Model))) {
    if ($NoSetup) {
        throw "MediaPipe benchmark environment is missing and -NoSetup was requested."
    }
    & (Join-Path $PSScriptRoot "setup-touchplus-mediapipe-benchmark.ps1") -Venv $Venv -Assets $Assets
    if ($LASTEXITCODE -ne 0) { throw "benchmark setup failed" }
}

& $Python $Probe `
    --input $InputPath `
    --output $Output `
    --model $Model `
    --eye $Eye `
    --num-hands $NumHands `
    --min-detection $MinDetection `
    --min-presence $MinPresence

if ($LASTEXITCODE -ne 0) { throw "MediaPipe benchmark failed with exit code $LASTEXITCODE" }

Write-Host ""
Write-Host "Benchmark complete."
Write-Host "Open overlays: $Output\annotations"
Write-Host "Summary JSON:  $Output\summary.json"
Write-Host "Summary CSV:   $Output\summary.csv"
