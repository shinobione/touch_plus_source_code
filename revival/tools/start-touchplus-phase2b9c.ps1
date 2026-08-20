param(
    [string]$Python = "python",
    [string]$Assets = ".\landmark-assets",
    [string]$Venv = ".\.touchplus-landmark-venv",
    [switch]$SkipSetup
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$tracker = Join-Path $root "touchplus_phase2b_tracker.exe"
$sidecar = Join-Path $root "touchplus_landmark_sidecar_live.py"
$setup = Join-Path $root "setup-touchplus-landmark-probe.ps1"
$venvPython = Join-Path $root ".touchplus-landmark-venv\Scripts\python.exe"
$assetsPath = Join-Path $root "landmark-assets"

if (-not (Test-Path $tracker)) { throw "Missing tracker: $tracker" }
if (-not (Test-Path $sidecar)) { throw "Missing sidecar: $sidecar" }

if (-not $SkipSetup -and ((-not (Test-Path $venvPython)) -or (-not (Test-Path $assetsPath)))) {
    Write-Host "[2B.9C] Landmark environment/assets missing; bootstrapping OpenCV Zoo..." -ForegroundColor Cyan
    if (-not (Test-Path $setup)) { throw "Missing setup script: $setup" }
    & powershell.exe -ExecutionPolicy Bypass -File $setup -Python $Python -Assets $Assets -Venv $Venv
    if ($LASTEXITCODE -ne 0) { throw "Landmark setup failed: $LASTEXITCODE" }
}

if (-not (Test-Path $venvPython)) { throw "Missing venv Python: $venvPython" }
if (-not (Test-Path $assetsPath)) { throw "Missing landmark assets: $assetsPath" }

$stdout = Join-Path $root "touchplus-anatomy-sidecar.log"
$stderr = Join-Path $root "touchplus-anatomy-sidecar-error.log"
Remove-Item $stdout,$stderr -Force -ErrorAction SilentlyContinue

Write-Host "[2B.9C] Starting anatomical sidecar..." -ForegroundColor Cyan
$sidecarProcess = Start-Process `
    -FilePath $venvPython `
    -ArgumentList @($sidecar, "--assets", $assetsPath) `
    -WorkingDirectory $root `
    -RedirectStandardOutput $stdout `
    -RedirectStandardError $stderr `
    -PassThru `
    -WindowStyle Hidden

Start-Sleep -Milliseconds 400
if ($sidecarProcess.HasExited) {
    Write-Host "[2B.9C] Sidecar exited early." -ForegroundColor Red
    if (Test-Path $stdout) { Get-Content $stdout }
    if (Test-Path $stderr) { Get-Content $stderr }
    throw "Anatomy sidecar failed to start."
}

Write-Host "[2B.9C] Starting Touch+ tracker. Close it with Q/ESC; sidecar will stop automatically." -ForegroundColor Green
Write-Host "[2B.9C] Sidecar log: $stdout"

try {
    & $tracker
    $trackerExit = $LASTEXITCODE
}
finally {
    if ($sidecarProcess -and -not $sidecarProcess.HasExited) {
        Stop-Process -Id $sidecarProcess.Id -Force -ErrorAction SilentlyContinue
        $sidecarProcess.WaitForExit(3000) | Out-Null
    }
}

if (Test-Path $stderr) {
    $err = Get-Content $stderr -Raw
    if ($err.Trim()) {
        Write-Host "[2B.9C] Sidecar stderr:" -ForegroundColor Yellow
        Write-Host $err
    }
}

exit $trackerExit
