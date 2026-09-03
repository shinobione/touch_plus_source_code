param(
    [string]$Python = "",
    [string]$Venv = "",
    [string]$Assets = ""
)

$ErrorActionPreference = "Stop"
$StateRoot = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA "TouchPlus\MediaPipeBenchmark" } else { Join-Path $env:TEMP "TouchPlus-MediaPipeBenchmark" }
if (-not $Venv) { $Venv = Join-Path $StateRoot "venv" }
if (-not $Assets) { $Assets = Join-Path $StateRoot "assets" }

$ModelUrl = "https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task"
$ModelPath = Join-Path $Assets "models\hand_landmarker.task"
$ProvenancePath = Join-Path $Assets "model-provenance.json"

function Resolve-Python {
    param([string]$Requested)
    if ($Requested) { return @($Requested) }
    if (Get-Command py -ErrorAction SilentlyContinue) {
        foreach ($selector in @("-3.12", "-3.11", "-3.10", "-3.9")) {
            & py $selector -c "import sys; print(sys.executable)" *> $null
            if ($LASTEXITCODE -eq 0) { return @("py", $selector) }
        }
        return @("py", "-3")
    }
    if (Get-Command python -ErrorAction SilentlyContinue) { return @("python") }
    throw "Python 3 not found. Install Python 3.9+ or pass -Python <path>."
}

function Invoke-Python {
    param([string[]]$Command, [string[]]$Arguments)
    $exe = $Command[0]
    $prefix = @()
    if ($Command.Count -gt 1) { $prefix = $Command[1..($Command.Count - 1)] }
    & $exe @prefix @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed: $exe $($prefix -join ' ') $($Arguments -join ' ')"
    }
}

$pythonCmd = Resolve-Python $Python
Write-Host "Using Python: $($pythonCmd -join ' ')"

if (-not (Test-Path $Venv)) {
    Invoke-Python $pythonCmd @("-m", "venv", $Venv)
}
$VenvPython = Join-Path $Venv "Scripts\python.exe"
if (-not (Test-Path $VenvPython)) { throw "Virtualenv Python missing: $VenvPython" }

& $VenvPython -m pip install --disable-pip-version-check --upgrade pip
if ($LASTEXITCODE -ne 0) { throw "pip upgrade failed" }
& $VenvPython -m pip install --disable-pip-version-check "mediapipe==1.0.1" "opencv-python>=4.10,<5" "numpy>=1.26,<3"
if ($LASTEXITCODE -ne 0) { throw "dependency install failed" }

New-Item -ItemType Directory -Force (Split-Path -Parent $ModelPath) | Out-Null
if (-not (Test-Path $ModelPath)) {
    Write-Host "Downloading official Google Hand Landmarker model bundle..."
    Invoke-WebRequest $ModelUrl -OutFile $ModelPath
}
$ModelSha = (Get-FileHash $ModelPath -Algorithm SHA256).Hash.ToLowerInvariant()
$MediaPipeVersion = (& $VenvPython -c "import importlib.metadata as m; print(m.version('mediapipe'))").Trim()

[ordered]@{
    schema = "touchplus-mediapipe-model-provenance-v1"
    model_url = $ModelUrl
    model_path = $ModelPath
    model_sha256 = $ModelSha
    mediapipe_version = $MediaPipeVersion
    verified_utc = [DateTime]::UtcNow.ToString("o")
} | ConvertTo-Json | Set-Content -Encoding UTF8 $ProvenancePath

$Probe = Join-Path $PSScriptRoot "touchplus_mediapipe_benchmark.py"
& $VenvPython $Probe --self-test
if ($LASTEXITCODE -ne 0) { throw "benchmark self-test failed" }
& $VenvPython $Probe --model-smoke --model $ModelPath
if ($LASTEXITCODE -ne 0) { throw "MediaPipe model smoke failed" }

Write-Host ""
Write-Host "TouchPlus MediaPipe benchmark environment: READY"
Write-Host "Python:      $VenvPython"
Write-Host "MediaPipe:   $MediaPipeVersion"
Write-Host "Model:       $ModelPath"
Write-Host "Model SHA256:$ModelSha"
Write-Host "Provenance:  $ProvenancePath"
