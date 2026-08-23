param(
    [string]$Python = "python",
    [string]$Assets = ".\landmark-assets",
    [string]$Venv = ".\.touchplus-landmark-venv",
    [switch]$SkipSetup,
    [switch]$EnableHybridPromotion,
    [switch]$SelfTestQuoting
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

function Quote-NativeArgument([string]$Value) {
    if ($null -eq $Value) { return '""' }
    if ($Value.Contains('"')) { throw "Native argument contains an unsupported quote: $Value" }
    return '"' + $Value + '"'
}

function Start-QuotedPythonProcess(
    [string]$PythonExe,
    [string]$ScriptPath,
    [string]$AssetsDir,
    [string]$WorkingDir,
    [string]$StdoutPath,
    [string]$StderrPath
) {
    # Windows PowerShell 5.1 Start-Process joins ArgumentList entries into a
    # single native command line. Passing raw paths therefore breaks as soon
    # as the kit lives below e.g. "F:\Google Drive\...". Build the native
    # argument line explicitly and quote every path-bearing argument.
    $argumentLine = (Quote-NativeArgument $ScriptPath) +
        ' --assets ' + (Quote-NativeArgument $AssetsDir)

    return Start-Process `
        -FilePath $PythonExe `
        -ArgumentList $argumentLine `
        -WorkingDirectory $WorkingDir `
        -RedirectStandardOutput $StdoutPath `
        -RedirectStandardError $StderrPath `
        -PassThru `
        -WindowStyle Hidden
}

if ($SelfTestQuoting) {
    $testRoot = Join-Path ([System.IO.Path]::GetTempPath()) "Touch Plus Launcher Quoting Test"
    $testScript = Join-Path $testRoot "sidecar probe with spaces.py"
    $testAssets = Join-Path $testRoot "assets folder with spaces"
    $testStdout = Join-Path $testRoot "stdout log.txt"
    $testStderr = Join-Path $testRoot "stderr log.txt"

    Remove-Item $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $testAssets | Out-Null

    @'
import pathlib
import sys

if len(sys.argv) != 3:
    raise SystemExit("unexpected argv count: %r" % (sys.argv,))
if sys.argv[1] != "--assets":
    raise SystemExit("missing --assets: %r" % (sys.argv,))
if "sidecar probe with spaces.py" not in pathlib.Path(sys.argv[0]).name:
    raise SystemExit("script path was split: %r" % (sys.argv,))
if "assets folder with spaces" not in pathlib.Path(sys.argv[2]).name:
    raise SystemExit("assets path was split: %r" % (sys.argv,))
print("PHASE 2B.9C LAUNCHER QUOTING SELF-TEST: PASS")
'@ | Set-Content -Path $testScript -Encoding UTF8

    $testProcess = Start-QuotedPythonProcess `
        -PythonExe $Python `
        -ScriptPath $testScript `
        -AssetsDir $testAssets `
        -WorkingDir $testRoot `
        -StdoutPath $testStdout `
        -StderrPath $testStderr

    $testProcess.WaitForExit()
    if ($testProcess.ExitCode -ne 0) {
        if (Test-Path $testStdout) { Get-Content $testStdout }
        if (Test-Path $testStderr) { Get-Content $testStderr }
        throw "Phase 2B.9C launcher quoting self-test failed: $($testProcess.ExitCode)"
    }

    Get-Content $testStdout
    Remove-Item $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    exit 0
}

$tracker = Join-Path $root "touchplus_phase2b_tracker.exe"
$sidecar = Join-Path $root "touchplus_landmark_sidecar_live.py"
$setup = Join-Path $root "setup-touchplus-landmark-probe.ps1"

$venvPath = if ([System.IO.Path]::IsPathRooted($Venv)) { $Venv } else { Join-Path $root $Venv }
$assetsPath = if ([System.IO.Path]::IsPathRooted($Assets)) { $Assets } else { Join-Path $root $Assets }
$venvPython = Join-Path $venvPath "Scripts\python.exe"

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
$sidecarProcess = Start-QuotedPythonProcess `
    -PythonExe $venvPython `
    -ScriptPath $sidecar `
    -AssetsDir $assetsPath `
    -WorkingDir $root `
    -StdoutPath $stdout `
    -StderrPath $stderr

Start-Sleep -Milliseconds 400
if ($sidecarProcess.HasExited) {
    Write-Host "[2B.9C] Sidecar exited early." -ForegroundColor Red
    if (Test-Path $stdout) { Get-Content $stdout }
    if (Test-Path $stderr) { Get-Content $stderr }
    throw "Anatomy sidecar failed to start."
}

$promotionMode = if ($EnableHybridPromotion) { "ENABLED" } else { "DISABLED" }
Write-Host "[2B.10D] Starting Touch+ tracker | hybrid promotion=$promotionMode. Close it with Q/ESC; sidecar will stop automatically." -ForegroundColor Green
Write-Host "[2B.9C] Sidecar log: $stdout"

try {
    if ($EnableHybridPromotion) {
        & $tracker --enable-hybrid-promotion
    }
    else {
        & $tracker
    }
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