$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$statusPath = Join-Path $PSScriptRoot "build-probe-status.json"
$outRoot = Join-Path $PSScriptRoot "out"
$stage = Join-Path $outRoot "touchplus-ractiv-log-only"
$zip = Join-Path $outRoot "touchplus-ractiv-log-only-windows-x86.zip"
$manifestPath = Join-Path $outRoot "package-manifest.json"

if (!(Test-Path $statusPath)) {
    Write-Host "[RACTIV_RECOVERY] No build status; package skipped."
    exit 0
}

$status = Get-Content $statusPath -Raw | ConvertFrom-Json
if (-not $status.compile_succeeded) {
    Write-Host "[RACTIV_RECOVERY] Historical build did not compile yet; package skipped."
    exit 0
}

$exe = Join-Path $root "build\track_plus.exe"
if (!(Test-Path $exe)) {
    throw "Build reported success but build\track_plus.exe is missing"
}

Remove-Item $outRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item $exe $stage

$requiredDlls = @(
    "eSPAEAWBCtrl.dll",
    "eSPDI.dll",
    "EtLib.dll"
)
foreach ($name in $requiredDlls) {
    $src = Join-Path $root "build\$name"
    if (!(Test-Path $src)) { throw "Required runtime DLL missing: $name" }
    Copy-Item $src $stage
}

# Copy the historical runtime libraries actually shipped in the repository.
Get-ChildItem (Join-Path $root "build") -File | Where-Object {
    $_.Name -match '^opencv_.*249\.dll$' -or
    $_.Name -ieq 'turbojpeg.dll' -or
    $_.Name -match '^sfml-.*\.dll$'
} | Copy-Item -Destination $stage

$database = Join-Path $root "build\database"
if (Test-Path $database) {
    Copy-Item $database (Join-Path $stage "database") -Recurse
}

$warning = @'
TOUCH+ RACTIV RECOVERY — LOG ONLY

This kit is an archaeology/diagnostic build.

Expected behavior:
- original Ractiv camera + hand/pose pipeline runs;
- legacy factory calibration/CDN path is bypassed;
- no PointerMapper 3D/contact output is trusted yet;
- win_cursor_plus is NOT packaged;
- Windows touch/mouse injection is NOT part of this kit.

Do not copy win_cursor_plus.exe into this folder for the R0/R1 smoke.
'@
Set-Content -Path (Join-Path $stage "RUN-LOG-ONLY.txt") -Value $warning -Encoding UTF8

# Belt-and-suspenders packaging gate: cursor/daemon/menu executables must not be present.
$forbidden = Get-ChildItem $stage -Recurse -File | Where-Object {
    $_.Name -match '^(win_cursor_plus|daemon_plus|menu_plus).*\.exe$'
}
if ($forbidden) {
    throw "FORBIDDEN output component packaged: $($forbidden.FullName -join ', ')"
}

$files = Get-ChildItem $stage -Recurse -File | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($stage.Length + 1).Replace('\\','/')
        bytes = $_.Length
        sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$manifest = [ordered]@{
    boundary = "R0/R1 LOG_ONLY"
    os_injection_packaged = $false
    win_cursor_plus_packaged = $false
    file_count = @($files).Count
    files = @($files)
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content $manifestPath -Encoding UTF8

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
$zipHash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "[RACTIV_RECOVERY] LOG_ONLY package ready: $zip"
Write-Host "[RACTIV_RECOVERY] SHA-256: $zipHash"
