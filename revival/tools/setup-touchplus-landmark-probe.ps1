param(
    [string]$Python = "",
    [string]$Assets = ".\landmark-assets",
    [string]$Venv = ".\.touchplus-landmark-venv"
)

$ErrorActionPreference = "Stop"

function Resolve-Python {
    param([string]$Requested)
    if ($Requested) {
        return @($Requested)
    }
    if (Get-Command py -ErrorAction SilentlyContinue) {
        return @("py", "-3")
    }
    if (Get-Command python -ErrorAction SilentlyContinue) {
        return @("python")
    }
    throw "Python 3 not found. Install Python 3.10+ or pass -Python <path>."
}

function Invoke-Python {
    param(
        [string[]]$Command,
        [string[]]$Arguments
    )
    $exe = $Command[0]
    $prefix = @()
    if ($Command.Count -gt 1) {
        $prefix = $Command[1..($Command.Count - 1)]
    }
    & $exe @prefix @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed: $exe $($prefix -join ' ') $($Arguments -join ' ')"
    }
}

function Download-Checked {
    param(
        [string]$Url,
        [string]$Path,
        [string]$Sha256 = ""
    )
    $parent = Split-Path -Parent $Path
    if ($parent) {
        New-Item -ItemType Directory -Force $parent | Out-Null
    }
    if (-not (Test-Path $Path)) {
        Write-Host "Downloading $Url"
        Invoke-WebRequest $Url -OutFile $Path
    }
    if ($Sha256) {
        $actual = (Get-FileHash $Path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $Sha256.ToLowerInvariant()) {
            Remove-Item $Path -Force -ErrorAction SilentlyContinue
            throw "SHA-256 mismatch for $Path`nexpected=$Sha256`nactual=$actual"
        }
    }
}

$pythonCmd = Resolve-Python $Python
Write-Host "Using Python: $($pythonCmd -join ' ')"

if (-not (Test-Path $Venv)) {
    Invoke-Python $pythonCmd @("-m", "venv", $Venv)
}

$venvPython = Join-Path $Venv "Scripts\python.exe"
if (-not (Test-Path $venvPython)) {
    throw "Virtualenv Python missing: $venvPython"
}

& $venvPython -m pip install --disable-pip-version-check --upgrade pip
if ($LASTEXITCODE -ne 0) { throw "pip upgrade failed" }
& $venvPython -m pip install --disable-pip-version-check "numpy>=1.26" "opencv-python>=4.10,<5"
if ($LASTEXITCODE -ne 0) { throw "dependency install failed" }

$modules = Join-Path $Assets "modules"
$models = Join-Path $Assets "models"

Download-Checked `
    "https://raw.githubusercontent.com/opencv/opencv_zoo/main/models/palm_detection_mediapipe/mp_palmdet.py" `
    (Join-Path $modules "mp_palmdet.py")

Download-Checked `
    "https://raw.githubusercontent.com/opencv/opencv_zoo/main/models/handpose_estimation_mediapipe/mp_handpose.py" `
    (Join-Path $modules "mp_handpose.py")

Download-Checked `
    "https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models/palm_detection_mediapipe/palm_detection_mediapipe_2023feb.onnx" `
    (Join-Path $models "palm_detection_mediapipe_2023feb.onnx") `
    "78ff51c38496b7fc8b8ebdb6cc8c1abb02fa6c38427c6848254cdaba57fcce7c"

Download-Checked `
    "https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models/handpose_estimation_mediapipe/handpose_estimation_mediapipe_2023feb.onnx" `
    (Join-Path $models "handpose_estimation_mediapipe_2023feb.onnx") `
    "db0898ae717b76b075d9bf563af315b29562e11f8df5027a1ef07b02bef6d81c"

Write-Host ""
Write-Host "TouchPlus landmark probe assets are ready."
Write-Host "Python: $venvPython"
Write-Host "Assets: $Assets"
Write-Host ""
Write-Host "Example:"
Write-Host "& `"$venvPython`" .\touchplus_landmark_probe.py --input .\landmark-captures --assets `"$Assets`""
