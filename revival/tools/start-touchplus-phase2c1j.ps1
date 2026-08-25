param(
    [string]$Python = "python",
    [string]$Assets = ".\landmark-assets",
    [string]$Venv = ".\.touchplus-landmark-venv",
    [string]$BenchCache = $env:TOUCHPLUS_BENCH_CACHE,
    [switch]$SkipSetup,
    [switch]$EnableHybridPromotion,
    [switch]$SelfTest
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

function Mount-BenchCacheDirectory([string]$Name) {
    if ([string]::IsNullOrWhiteSpace($BenchCache)) { return }
    $cacheRoot = if ([System.IO.Path]::IsPathRooted($BenchCache)) {
        $BenchCache
    } else {
        Join-Path $root $BenchCache
    }
    $source = Join-Path $cacheRoot $Name
    $destination = Join-Path $root $Name
    if (Test-Path $destination) { return }
    if (-not (Test-Path $source)) { return }
    try {
        New-Item -ItemType Junction -Path $destination -Target $source -ErrorAction Stop | Out-Null
        Write-Host "[BENCH CACHE] Mounted $Name -> $source" -ForegroundColor DarkCyan
    }
    catch {
        Write-Host "[BENCH CACHE] Junction unavailable for $Name; copying cached directory..." -ForegroundColor Yellow
        Copy-Item -LiteralPath $source -Destination $destination -Recurse -Force
    }
}

$acceptedLauncher = Join-Path $root "start-touchplus-phase2b9c.ps1"
$shadowSidecar = Join-Path $root "touchplus_landmark_sidecar_shadow_v2c1j.py"
$setup = Join-Path $root "setup-touchplus-landmark-probe.ps1"

if (-not (Test-Path $acceptedLauncher)) { throw "Missing accepted launcher: $acceptedLauncher" }
if (-not (Test-Path $shadowSidecar)) { throw "Missing 2C.1J shadow sidecar: $shadowSidecar" }

if ($SelfTest) {
    Write-Host "[2C.1J] Running accepted launcher quoting regression..." -ForegroundColor Cyan
    & powershell.exe -ExecutionPolicy Bypass -File $acceptedLauncher -SelfTestQuoting -Python $Python
    if ($LASTEXITCODE -ne 0) { throw "Accepted launcher quoting regression failed: $LASTEXITCODE" }
    Write-Host "[2C.1J] Launcher wrapper self-test PASS" -ForegroundColor Green
    exit 0
}

Mount-BenchCacheDirectory ".touchplus-landmark-venv"
Mount-BenchCacheDirectory "landmark-assets"

$venvPath = if ([System.IO.Path]::IsPathRooted($Venv)) { $Venv } else { Join-Path $root $Venv }
$assetsPath = if ([System.IO.Path]::IsPathRooted($Assets)) { $Assets } else { Join-Path $root $Assets }
$venvPython = Join-Path $venvPath "Scripts\python.exe"

if (-not $SkipSetup -and ((-not (Test-Path $venvPython)) -or (-not (Test-Path $assetsPath)))) {
    Write-Host "[2C.1J] Landmark environment/assets missing; bootstrapping OpenCV Zoo..." -ForegroundColor Cyan
    if (-not (Test-Path $setup)) { throw "Missing setup script: $setup" }
    & powershell.exe -ExecutionPolicy Bypass -File $setup -Python $Python -Assets $Assets -Venv $Venv
    if ($LASTEXITCODE -ne 0) { throw "Landmark setup failed: $LASTEXITCODE" }
}

if (-not (Test-Path $venvPython)) { throw "Missing venv Python: $venvPython" }
if (-not (Test-Path $assetsPath)) { throw "Missing landmark assets: $assetsPath" }

$shadowStdout = Join-Path $root "touchplus-anatomy-shadow-sidecar.log"
$shadowStderr = Join-Path $root "touchplus-anatomy-shadow-sidecar-error.log"
Remove-Item $shadowStdout,$shadowStderr -Force -ErrorAction SilentlyContinue

Write-Host "[2C.1J] Starting isolated ungated SHADOW anatomy sidecar..." -ForegroundColor Magenta
$shadowProcess = Start-QuotedPythonProcess `
    -PythonExe $venvPython `
    -ScriptPath $shadowSidecar `
    -AssetsDir $assetsPath `
    -WorkingDir $root `
    -StdoutPath $shadowStdout `
    -StderrPath $shadowStderr

Start-Sleep -Milliseconds 400
if ($shadowProcess.HasExited) {
    Write-Host "[2C.1J] Shadow sidecar exited early." -ForegroundColor Red
    if (Test-Path $shadowStdout) { Get-Content $shadowStdout }
    if (Test-Path $shadowStderr) { Get-Content $shadowStderr }
    throw "Phase 2C.1J shadow anatomy sidecar failed to start."
}

Write-Host "[2C.1J] Shadow log: $shadowStdout" -ForegroundColor DarkCyan
Write-Host "[2C.1J] IPC=SEPARATE | accepted_pipeline_consumes_shadow=NO | OS_INJECTION=DISABLED" -ForegroundColor Green

$baseArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", $acceptedLauncher,
    "-Python", $Python,
    "-Assets", $assetsPath,
    "-Venv", $venvPath,
    "-SkipSetup"
)
if (-not [string]::IsNullOrWhiteSpace($BenchCache)) {
    $baseArgs += @("-BenchCache", $BenchCache)
}
if ($EnableHybridPromotion) {
    $baseArgs += "-EnableHybridPromotion"
}

try {
    & powershell.exe @baseArgs
    $trackerExit = $LASTEXITCODE
}
finally {
    if ($shadowProcess -and -not $shadowProcess.HasExited) {
        Stop-Process -Id $shadowProcess.Id -Force -ErrorAction SilentlyContinue
        $shadowProcess.WaitForExit(3000) | Out-Null
    }
}

if (Test-Path $shadowStderr) {
    $err = Get-Content $shadowStderr -Raw
    if (-not [string]::IsNullOrWhiteSpace($err)) {
        Write-Host "[2C.1J] Shadow sidecar stderr:" -ForegroundColor Yellow
        Write-Host $err
    }
}

exit $trackerExit
