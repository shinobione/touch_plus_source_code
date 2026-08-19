param(
    [int]$Pairs = 20,
    [string]$Serial = '0101007379',
    [string]$OutputRoot = 'calibration-captures',
    [switch]$LegacyInit,
    [switch]$NoOpenPreview
)

$ErrorActionPreference = 'Stop'

Write-Host 'TouchPlus Revival - Phase 1B.2a local stereo calibration capture' -ForegroundColor Cyan
Write-Host 'Guided capture of synchronized LEFT/RIGHT image pairs using the proven atomic probe.'
Write-Host ''

if ($Pairs -lt 8) { throw 'Pairs must be at least 8. Recommended: 18-25.' }
if ([string]::IsNullOrWhiteSpace($Serial)) { throw 'Serial must not be empty.' }

$scriptDir = Split-Path -Parent $PSCommandPath
$probe = Join-Path $scriptDir 'touchplus_atomic_probe.exe'
if (-not (Test-Path $probe)) {
    throw 'touchplus_atomic_probe.exe is missing next to this script. Use the packaged Phase 1B.2a kit.'
}

foreach ($dll in @('eSPAEAWBCtrl.dll','eSPDI.dll','EtLib.dll')) {
    if (-not (Test-Path (Join-Path $scriptDir $dll))) {
        throw "$dll is missing next to this script. Use the packaged Phase 1B.2a kit."
    }
}

$target = Join-Path $scriptDir 'calibration-target-9x6-25mm-a4.svg'
if (-not (Test-Path $target)) {
    Write-Warning 'Calibration target SVG is missing from the kit.'
}

$sessionDir = Join-Path $scriptDir (Join-Path $OutputRoot $Serial)
$rawDir = Join-Path $sessionDir 'raw'
$tempDir = Join-Path $scriptDir 'touchplus-atomic'
New-Item -ItemType Directory -Force -Path $rawDir | Out-Null

$metadata = [ordered]@{
    schema = 'touchplus-revival-calibration-session-v1'
    serial = $Serial
    captured_at = (Get-Date).ToString('o')
    source = 'touchplus_atomic_probe.exe'
    stereo_frame = '1280x480 split into 640x480 left + 640x480 right'
    physical_baseline_fps = '~30 fps measured; advertised mode is 60 fps'
    capture_orientation = 'raw Phase0C orientation; solver must apply historical Ractiv vertical flip before calibration/runtime comparison'
    board = [ordered]@{
        type = 'checkerboard'
        inner_corners_x = 9
        inner_corners_y = 6
        square_mm = 25.0
        printed_board_squares_x = 10
        printed_board_squares_y = 7
        printed_board_width_mm = 250.0
        printed_board_height_mm = 175.0
        target_file = 'calibration-target-9x6-25mm-a4.svg'
    }
    requested_pairs = $Pairs
    legacy_init = [bool]$LegacyInit
}
$metadata | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $sessionDir 'session.json')

Write-Host ('Device serial : {0}' -f $Serial) -ForegroundColor Green
Write-Host ('Target        : 9x6 inner corners, 25 mm squares')
Write-Host ('Pairs wanted  : {0}' -f $Pairs)
Write-Host ('Output        : {0}' -f $sessionDir)
Write-Host ''
Write-Host 'Capture rules:' -ForegroundColor Yellow
Write-Host '  - Keep the ENTIRE checkerboard visible in BOTH left and right images.'
Write-Host '  - Hold the board flat and still during each capture.'
Write-Host '  - Vary distance, left/right/up/down position, yaw, pitch and a little roll.'
Write-Host '  - Avoid blur, glare, repeated near-identical poses and extreme edge clipping.'
Write-Host '  - Recommended: 18-25 accepted pairs.'
Write-Host ''

# Continue after existing accepted pairs if the script is restarted.
$existing = @(Get-ChildItem $rawDir -Filter 'pair-*-left.png' -ErrorAction SilentlyContinue)
$accepted = $existing.Count
if ($accepted -gt 0) {
    Write-Host ("Resuming session with {0} existing accepted pair(s)." -f $accepted) -ForegroundColor Yellow
}

function Remove-TempProbeOutput {
    Remove-Item (Join-Path $tempDir 'touchplus-full.png') -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $tempDir 'touchplus-left.png') -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $tempDir 'touchplus-right.png') -Force -ErrorAction SilentlyContinue
}

function Capture-OnePair([int]$index) {
    Remove-TempProbeOutput

    $args = @()
    if ($LegacyInit) { $args += '--legacy-init' }

    Write-Host ''
    Write-Host ('Capturing pair {0:D3}...' -f $index) -ForegroundColor Cyan
    Push-Location $scriptDir
    try {
        & $probe @args
        $exit = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    if ($exit -ne 0) {
        throw "Atomic probe failed with exit code $exit. Do not accept this pair."
    }

    $srcFull = Join-Path $tempDir 'touchplus-full.png'
    $srcLeft = Join-Path $tempDir 'touchplus-left.png'
    $srcRight = Join-Path $tempDir 'touchplus-right.png'
    foreach ($path in @($srcFull,$srcLeft,$srcRight)) {
        if (-not (Test-Path $path)) { throw "Expected probe output missing: $path" }
    }

    $stem = 'pair-{0:D3}' -f $index
    $dstFull = Join-Path $rawDir ($stem + '-full.png')
    $dstLeft = Join-Path $rawDir ($stem + '-left.png')
    $dstRight = Join-Path $rawDir ($stem + '-right.png')
    Copy-Item $srcFull $dstFull -Force
    Copy-Item $srcLeft $dstLeft -Force
    Copy-Item $srcRight $dstRight -Force

    $hashLeft = (Get-FileHash $dstLeft -Algorithm SHA256).Hash
    $hashRight = (Get-FileHash $dstRight -Algorithm SHA256).Hash
    [ordered]@{
        pair = $index
        accepted_at = (Get-Date).ToString('o')
        left = (Split-Path $dstLeft -Leaf)
        right = (Split-Path $dstRight -Leaf)
        full = (Split-Path $dstFull -Leaf)
        sha256_left = $hashLeft
        sha256_right = $hashRight
    } | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $rawDir ($stem + '.json'))

    if (-not $NoOpenPreview) {
        try { Start-Process $dstFull | Out-Null } catch { Write-Warning $_.Exception.Message }
    }

    return [pscustomobject]@{ Full=$dstFull; Left=$dstLeft; Right=$dstRight; Stem=$stem }
}

while ($accepted -lt $Pairs) {
    $next = $accepted + 1
    Write-Host ''
    Write-Host ('POSE {0}/{1}' -f $next, $Pairs) -ForegroundColor Green
    Write-Host 'Move the checkerboard to a NEW pose, keep it fully visible in both eyes, then hold still.'
    $cmd = Read-Host '[ENTER]=capture   O=open target   Q=finish early'
    if ($cmd -match '^[Qq]$') { break }
    if ($cmd -match '^[Oo]$') {
        if (Test-Path $target) { Start-Process $target | Out-Null }
        continue
    }

    try {
        $pair = Capture-OnePair $next
    }
    catch {
        Write-Host ('CAPTURE FAILED: {0}' -f $_.Exception.Message) -ForegroundColor Red
        Write-Host 'Fix the issue and retry this same pose number.'
        continue
    }

    while ($true) {
        Write-Host ('Saved {0}' -f $pair.Stem) -ForegroundColor Green
        $review = Read-Host '[ENTER]=accept   R=retake   O=open preview   Q=accept + finish'
        if ($review -match '^[Rr]$') {
            Remove-Item $pair.Full,$pair.Left,$pair.Right -Force -ErrorAction SilentlyContinue
            Remove-Item (Join-Path $rawDir ($pair.Stem + '.json')) -Force -ErrorAction SilentlyContinue
            Write-Host 'Pair rejected; retake it with a cleaner/different pose.' -ForegroundColor Yellow
            break
        }
        if ($review -match '^[Oo]$') {
            try { Start-Process $pair.Full | Out-Null } catch { Write-Warning $_.Exception.Message }
            continue
        }

        $accepted++
        if ($review -match '^[Qq]$') { $accepted = [Math]::Min($accepted, $Pairs); break 2 }
        break
    }
}

$acceptedPairs = @(Get-ChildItem $rawDir -Filter 'pair-*-left.png' -ErrorAction SilentlyContinue).Count
$summary = [ordered]@{
    serial = $Serial
    accepted_pairs = $acceptedPairs
    requested_pairs = $Pairs
    finished_at = (Get-Date).ToString('o')
    status = if ($acceptedPairs -ge 12) { 'READY_FOR_SOLVER' } elseif ($acceptedPairs -ge 8) { 'USABLE_BUT_MORE_PAIRS_RECOMMENDED' } else { 'INSUFFICIENT' }
}
$summary | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $sessionDir 'capture-summary.json')

$zipPath = Join-Path $scriptDir ('touchplus-calibration-{0}.zip' -f $Serial)
Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path $sessionDir -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host ''
Write-Host '========== CALIBRATION CAPTURE RESULT ==========' -ForegroundColor Cyan
Write-Host ('Accepted pairs : {0}' -f $acceptedPairs)
Write-Host ('Dataset        : {0}' -f $sessionDir)
Write-Host ('ZIP            : {0}' -f $zipPath) -ForegroundColor Green
if ($acceptedPairs -ge 12) {
    Write-Host 'RESULT: READY_FOR_SOLVER' -ForegroundColor Green
} elseif ($acceptedPairs -ge 8) {
    Write-Host 'RESULT: USABLE_BUT_MORE_PAIRS_RECOMMENDED' -ForegroundColor Yellow
} else {
    Write-Host 'RESULT: INSUFFICIENT - capture at least 8, preferably 18-25 pairs.' -ForegroundColor Red
}
Write-Host '================================================'
