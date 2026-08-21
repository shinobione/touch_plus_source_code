param(
    [string]$Configuration = "Release",
    [string]$Platform = "Win32"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$log = Join-Path $PSScriptRoot "build-probe.log"
$status = Join-Path $PSScriptRoot "build-probe-status.json"

$env:ETRON_DIR = (Resolve-Path (Join-Path $root "dependencies\Etron")).Path
$env:OPENCV_DIR = (Resolve-Path (Join-Path $root "dependencies\OpenCV\build\x86\vc12")).Path
$env:TURBOJPEG_DIR = (Resolve-Path (Join-Path $root "dependencies\TurboJPEG")).Path
$env:DIRECTSHOW_DIR = (Resolve-Path (Join-Path $root "dependencies\DirectShow")).Path
$env:SFML_DIR = (Resolve-Path (Join-Path $root "dependencies\SFML")).Path

$project = Join-Path $root "track_plus_visual_studio\track_plus\track_plus.vcxproj"
$patch = Join-Path $root "ractiv_recovery\patches\0001-log-only-bringup.patch"

Write-Host "[RACTIV_RECOVERY] Applying LOG_ONLY compatibility patch to CI checkout"
& git -C $root apply $patch
if ($LASTEXITCODE -ne 0) {
    throw "git apply failed"
}

$msbuild = (Get-Command msbuild.exe -ErrorAction Stop).Source
Write-Host "[RACTIV_RECOVERY] MSBuild: $msbuild"
Write-Host "[RACTIV_RECOVERY] This is an archaeology probe, not yet a release gate."

$args = @(
    $project,
    "/m",
    "/t:Build",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:PlatformToolset=v143",
    "/nologo",
    "/v:minimal"
)

$started = Get-Date
& $msbuild @args 2>&1 | Tee-Object -FilePath $log
$code = $LASTEXITCODE
$ended = Get-Date

$result = [ordered]@{
    boundary = "R0/R1 build archaeology"
    historical_project_toolset = "v120"
    probe_toolset = "v143"
    configuration = $Configuration
    platform = $Platform
    msbuild_exit_code = $code
    compile_succeeded = ($code -eq 0)
    started_utc = $started.ToUniversalTime().ToString("o")
    ended_utc = $ended.ToUniversalTime().ToString("o")
    note = "Failure is recorded as evidence for the next compatibility patch; this probe intentionally does not claim release readiness."
}

$result | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $status
$result | Format-List

# Do not turn historical incompatibility into a red source/safety gate. The JSON
# and full log are uploaded by Actions and become the exact next-work evidence.
exit 0
