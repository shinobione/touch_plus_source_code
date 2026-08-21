param(
    [ValidateSet("Release", "RelWithDebInfo")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $PSScriptRoot "cmake-build-win32"
$log = Join-Path $PSScriptRoot "minimal-build.log"
$statusPath = Join-Path $PSScriptRoot "minimal-build-status.json"

Remove-Item $log -Force -ErrorAction SilentlyContinue
Remove-Item $statusPath -Force -ErrorAction SilentlyContinue

$started = Get-Date
$configureCode = -1
$buildCode = -1
$exe = Join-Path $buildDir "out\touchplus_ractiv_log_only.exe"

function Write-Status([string]$phase, [string]$note) {
    $result = [ordered]@{
        boundary = "R0/R1 minimal LOG_ONLY build"
        architecture = "Win32/x86"
        configuration = $Configuration
        phase = $phase
        cmake_configure_exit_code = $configureCode
        cmake_build_exit_code = $buildCode
        compile_succeeded = (($configureCode -eq 0) -and ($buildCode -eq 0) -and (Test-Path $exe))
        executable = if (Test-Path $exe) { $exe } else { $null }
        started_utc = $started.ToUniversalTime().ToString("o")
        ended_utc = (Get-Date).ToUniversalTime().ToString("o")
        safety = [ordered]@{
            pointer_mapper_compiled = $false
            ipc_compiled = $false
            udp_compiled = $false
            win_cursor_plus_compiled = $false
            os_injection_enabled = $false
        }
        note = $note
    }
    $result | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $statusPath
    $result | Format-List
}

try {
    $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
    Write-Host "[RACTIV_RECOVERY] CMake: $cmake" | Tee-Object -FilePath $log -Append
    Write-Host "[RACTIV_RECOVERY] Configure minimal LOG_ONLY target for Win32/x86" | Tee-Object -FilePath $log -Append

    if (Test-Path $buildDir) {
        Remove-Item $buildDir -Recurse -Force
    }

    & $cmake `
        -S $PSScriptRoot `
        -B $buildDir `
        -A Win32 `
        -DCMAKE_CONFIGURATION_TYPES="$Configuration" `
        2>&1 | Tee-Object -FilePath $log -Append
    $configureCode = $LASTEXITCODE

    if ($configureCode -ne 0) {
        Write-Status "configure-failed" "CMake configuration failed; inspect minimal-build.log for the first exact compatibility blocker."
        exit 0
    }

    Write-Host "[RACTIV_RECOVERY] Build touchplus_ractiv_log_only" | Tee-Object -FilePath $log -Append
    & $cmake --build $buildDir --config $Configuration --target touchplus_ractiv_log_only -- /m `
        2>&1 | Tee-Object -FilePath $log -Append
    $buildCode = $LASTEXITCODE

    if (($buildCode -eq 0) -and (Test-Path $exe)) {
        Write-Status "compiled" "Minimal LOG_ONLY executable compiled. Packaging/safety gate must pass before physical smoke."
    }
    else {
        Write-Status "compile-failed" "Compilation/link failed; inspect minimal-build.log and patch only the concrete blocker."
    }
}
catch {
    $_ | Out-String | Tee-Object -FilePath $log -Append | Write-Host
    Write-Status "probe-exception" $_.Exception.Message
}

# Keep this probe non-destructive and evidence-producing. CI performs an
# explicit gate after uploading logs, so failures remain inspectable.
exit 0
