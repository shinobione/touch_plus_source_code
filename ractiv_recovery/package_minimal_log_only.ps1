$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$statusPath = Join-Path $PSScriptRoot "minimal-build-status.json"
$buildDir = Join-Path $PSScriptRoot "cmake-build-win32"
$exe = Join-Path $buildDir "out\touchplus_ractiv_log_only.exe"
$outRoot = Join-Path $PSScriptRoot "out-minimal"
$stage = Join-Path $outRoot "touchplus-ractiv-log-only"
$zip = Join-Path $outRoot "touchplus-ractiv-log-only-windows-x86.zip"
$manifestPath = Join-Path $outRoot "package-manifest.json"

if (!(Test-Path $statusPath)) {
    Write-Host "[RACTIV_RECOVERY] No minimal build status; package skipped."
    exit 0
}

$status = Get-Content $statusPath -Raw | ConvertFrom-Json
if (-not $status.compile_succeeded) {
    Write-Host "[RACTIV_RECOVERY] Minimal runtime has not compiled yet; package skipped."
    exit 0
}

if (!(Test-Path $exe)) {
    throw "Minimal build reported success but executable is missing: $exe"
}

Remove-Item $outRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item $exe $stage

$requiredRuntime = @(
    "eSPAEAWBCtrl.dll",
    "eSPDI.dll",
    "EtLib.dll",
    "turbojpeg.dll"
)

foreach ($name in $requiredRuntime) {
    $src = Join-Path $root "build\$name"
    if (!(Test-Path $src)) {
        throw "Required historical runtime DLL missing: $name"
    }
    Copy-Item $src $stage
}

# Copy the complete bundled OpenCV 2.4.9 runtime family. The minimal target
# links only a subset, but dependent OpenCV modules may load sibling modules;
# carrying the shipped family is safer than guessing transitive DLL imports.
Get-ChildItem (Join-Path $root "build") -File | Where-Object {
    $_.Name -match '^opencv_.*249\.dll$'
} | Copy-Item -Destination $stage

# PoseEstimator reads the historical pose database locally. No menu/daemon/UI
# directories are needed or allowed in this recovery package.
$database = Join-Path $root "build\database"
if (Test-Path $database) {
    Copy-Item $database (Join-Path $stage "database") -Recurse
}

$warning = @'
TOUCH+ RACTIV RECOVERY — R1 LOG_ONLY ANATOMY DIAGNOSTIC

This package is a diagnostic hardware build, not the historical product.

Compiled path:
  Touch+ Camera
    -> MotionProcessorNew
    -> ForegroundExtractorNew
    -> HandSplitterNew
    -> MonoProcessorNew
    -> Recovery raw-eye index refiner
    -> PoseEstimator telemetry / diagnostic viewer

The raw-eye index refiner recovers only the useful local full-resolution idea
from the historical HandResolver. It does NOT compile or use the historical
HandResolver implementation or its dead-CDN Reprojector dependency.

SAFETY BOUNDARY
- OS input injection is disabled.
- PointerMapper is not compiled into this executable.
- Historical Reprojector/contact output is not compiled into this executable.
- Historical HandResolver is not compiled into this executable.
- IPC/UDP cursor transport is not compiled into this executable.
- win_cursor_plus / fallback / daemon / menu executables are not packaged.

Diagnostic viewer:
  touchplus_ractiv_log_only.exe --viewer

Viewer legend:
- PALM = cyan
- COARSE INDEX (MonoProcessorNew 160x120 -> x4) = magenta
- REFINED INDEX (Recovery full-res raw eye) = green X
- THUMB = yellow

The smoke is observational only. Close Windows Camera and other software using
the Touch+ before launching the executable.
'@
Set-Content -Path (Join-Path $stage "RUN-LOG-ONLY.txt") -Value $warning -Encoding UTF8

# Belt-and-suspenders: no executable besides the minimal recovery executable is
# permitted anywhere in the package.
$allExes = @(Get-ChildItem $stage -Recurse -File -Filter "*.exe")
$unexpectedExes = @($allExes | Where-Object { $_.Name -ine "touchplus_ractiv_log_only.exe" })
if ($unexpectedExes.Count -gt 0) {
    throw "FORBIDDEN executable packaged: $($unexpectedExes.FullName -join ', ')"
}

$forbiddenNames = @(
    "win_cursor_plus",
    "win_cursor_plus_fallback",
    "daemon_plus",
    "menu_plus"
)
foreach ($forbidden in $forbiddenNames) {
    if (Get-ChildItem $stage -Recurse -Force | Where-Object { $_.Name -like "$forbidden*" }) {
        throw "FORBIDDEN historical output component packaged: $forbidden"
    }
}

# Static binary-string smoke. This is not a proof of absence of all APIs, but
# catches accidental compilation of the known historical output modules.
$bytes = [System.IO.File]::ReadAllBytes($exe)
$ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
$forbiddenStrings = @(
    "win_cursor_plus",
    "hide_cursor_index",
    "hide_cursor_thumb",
    "PointerMapper"
)
foreach ($needle in $forbiddenStrings) {
    if ($ascii.Contains($needle)) {
        throw "Safety gate failed: minimal executable contains forbidden output string '$needle'"
    }
}

$files = @(Get-ChildItem $stage -Recurse -File | Sort-Object FullName)
$manifest = [ordered]@{
    boundary = "R1 LOG_ONLY raw-eye anatomy diagnostic"
    executable = "touchplus_ractiv_log_only.exe"
    os_injection = "DISABLED"
    pointer_mapper_compiled = $false
    reprojector_compiled = $false
    historical_hand_resolver_compiled = $false
    recovery_raw_eye_index_refiner_compiled = $true
    ipc_compiled = $false
    udp_compiled = $false
    forbidden_historical_executables_packaged = $false
    files = @($files | ForEach-Object {
        [ordered]@{
            path = $_.FullName.Substring($stage.Length + 1)
            bytes = $_.Length
            sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $manifestPath

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal

$zipHash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "[RACTIV_RECOVERY] Safe R1 LOG_ONLY anatomy package created"
Write-Host "  ZIP: $zip"
Write-Host "  SHA-256: $zipHash"
Write-Host "  executable count: $($allExes.Count) (minimal runtime only)"
