param(
    [Parameter(Mandatory = $true)]
    [string]$ZipPath,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$revivalDir = Split-Path -Parent $toolsDir
$repoRoot = Split-Path -Parent $revivalDir
$cacheRoot = Join-Path $repoRoot ".touchplus-bench-cache"

$zipFullPath = (Resolve-Path -LiteralPath $ZipPath).Path

if (Test-Path $cacheRoot) {
    if (-not $Force) {
        Write-Host "[BENCH CACHE] Existing cache found: $cacheRoot" -ForegroundColor Yellow
        Write-Host "[BENCH CACHE] Re-run with -Force to replace it." -ForegroundColor Yellow
        exit 0
    }
    Remove-Item -LiteralPath $cacheRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
Write-Host "[BENCH CACHE] Extracting reusable bench data..." -ForegroundColor Cyan
Expand-Archive -LiteralPath $zipFullPath -DestinationPath $cacheRoot -Force

$required = @(
    ".touchplus-landmark-venv",
    "landmark-assets",
    "surface"
)

foreach ($name in $required) {
    $path = Join-Path $cacheRoot $name
    if (-not (Test-Path $path)) {
        throw "Bench cache install failed; missing expected directory: $path"
    }
}

$venvPython = Join-Path $cacheRoot ".touchplus-landmark-venv\Scripts\python.exe"
$palmdet = Join-Path $cacheRoot "landmark-assets\models\palm_detection_mediapipe_2023feb.onnx"
$handpose = Join-Path $cacheRoot "landmark-assets\models\handpose_estimation_mediapipe_2023feb.onnx"
$surfaceFiles = Get-ChildItem -LiteralPath (Join-Path $cacheRoot "surface") -Filter "*.json" -File

if (-not (Test-Path $venvPython)) { throw "Bench cache venv is incomplete: $venvPython" }
if (-not (Test-Path $palmdet)) { throw "Bench cache palm model is missing: $palmdet" }
if (-not (Test-Path $handpose)) { throw "Bench cache hand-pose model is missing: $handpose" }
if (-not $surfaceFiles) { throw "Bench cache surface directory contains no JSON surface frame." }

[Environment]::SetEnvironmentVariable("TOUCHPLUS_BENCH_CACHE", $cacheRoot, "User")
$env:TOUCHPLUS_BENCH_CACHE = $cacheRoot

Write-Host ""
Write-Host "[BENCH CACHE] READY" -ForegroundColor Green
Write-Host "[BENCH CACHE] Path: $cacheRoot"
Write-Host "[BENCH CACHE] User environment variable TOUCHPLUS_BENCH_CACHE has been set."
Write-Host "[BENCH CACHE] Future extracted kits can auto-mount the three cached directories."
Write-Host "[BENCH CACHE] The cache is gitignored and will not be committed/pushed."
