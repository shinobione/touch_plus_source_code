param(
    [switch]$TryFactoryDownload
)

$ErrorActionPreference = 'Stop'

Write-Host 'TouchPlus Revival - Phase 1B factory calibration probe' -ForegroundColor Cyan
Write-Host 'Reads the Touch+ flash serial and derives the historical Ractiv calibration key.'
Write-Host ''

# The recovered Etron SDK is Win32. Relaunch under 32-bit Windows PowerShell.
if ([Environment]::Is64BitProcess) {
    $ps32 = Join-Path $env:WINDIR 'SysWOW64\WindowsPowerShell\v1.0\powershell.exe'
    if (-not (Test-Path $ps32)) { throw "32-bit Windows PowerShell not found at $ps32" }

    $args32 = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath)
    if ($TryFactoryDownload) { $args32 += '-TryFactoryDownload' }
    Write-Host 'Relaunching under 32-bit Windows PowerShell for the Etron DLLs...'
    & $ps32 @args32
    exit $LASTEXITCODE
}

Write-Host ('Process architecture: {0}-bit' -f ([IntPtr]::Size * 8))

$dllDir = $PSScriptRoot
$required = @('eSPAEAWBCtrl.dll', 'eSPDI.dll', 'EtLib.dll')
foreach ($name in $required) {
    $path = Join-Path $dllDir $name
    if (-not (Test-Path $path)) {
        throw "Missing $name next to this script. Use the packaged Phase 1B kit."
    }
}

$env:PATH = "$dllDir;$env:PATH"
[Environment]::CurrentDirectory = $dllDir
Set-Location $dllDir

$interop = @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class TouchPlusFactoryInterop {
    [DllImport("eSPDI.dll", CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool EtronDI_Init(out IntPtr handle);

    [DllImport("eSPDI.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EtronDI_Release(ref IntPtr handle);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
    public static extern int eSPAEAWB_EnumDevice(out int deviceCount);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
    public static extern int eSPAEAWB_GetDevicename(int index, StringBuilder name, int maxCount);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_SelectDevice(int index);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_SetSensorType(int sensorType);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_SWUnlock(ushort appId);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_ReadFlash([Out] byte[] data, int length);
}
'@

Add-Type -TypeDefinition $interop -Language CSharp

function Show-Ret([string]$name, [int]$code) {
    $state = if ($code -eq 0) { 'OK' } else { 'RET=' + $code }
    Write-Host ('{0,-34} {1}' -f $name, $state)
    return ($code -eq 0)
}

$handle = [IntPtr]::Zero
try {
    if (-not [TouchPlusFactoryInterop]::EtronDI_Init([ref]$handle) -or $handle -eq [IntPtr]::Zero) {
        throw 'EtronDI_Init failed.'
    }

    $count = 0
    if (-not (Show-Ret 'eSPAEAWB_EnumDevice' ([TouchPlusFactoryInterop]::eSPAEAWB_EnumDevice([ref]$count)))) {
        throw 'Etron camera enumeration failed.'
    }

    $touchIndex = -1
    for ($i = 0; $i -lt $count; $i++) {
        $sb = New-Object System.Text.StringBuilder 255
        $ret = [TouchPlusFactoryInterop]::eSPAEAWB_GetDevicename($i, $sb, 255)
        Write-Host ('  [{0}] {1} (ret={2})' -f $i, $sb.ToString(), $ret)
        if ($touchIndex -lt 0 -and $sb.ToString() -like 'Touch+ Camera*') { $touchIndex = $i }
    }
    if ($touchIndex -lt 0) { throw 'Touch+ Camera not found by the Etron control DLL.' }

    if (-not (Show-Ret 'eSPAEAWB_SelectDevice' ([TouchPlusFactoryInterop]::eSPAEAWB_SelectDevice($touchIndex)))) { throw 'Touch+ selection failed.' }
    if (-not (Show-Ret 'eSPAEAWB_SetSensorType(OV7740)' ([TouchPlusFactoryInterop]::eSPAEAWB_SetSensorType(1)))) { throw 'Sensor selection failed.' }
    if (-not (Show-Ret 'eSPAEAWB_SWUnlock(0x0107)' ([TouchPlusFactoryInterop]::eSPAEAWB_SWUnlock([UInt16]0x0107)))) { throw 'Touch+ software unlock failed.' }

    $raw = New-Object byte[] 10
    $read = [TouchPlusFactoryInterop]::eSPAEAWB_ReadFlash($raw, 10)
    if (-not (Show-Ret 'eSPAEAWB_ReadFlash(10)' $read)) { throw 'Reading the Touch+ serial from flash failed.' }

    $rawText = ($raw | ForEach-Object { $_.ToString() }) -join ','
    # This deliberately mirrors Ractiv Camera::getSerialNumber(): decimal string
    # concatenation of each of the 10 flash bytes.
    $fullSerial = ($raw | ForEach-Object { $_.ToString() }) -join ''

    Write-Host ''
    Write-Host ('Raw flash bytes      : {0}' -f $rawText)
    Write-Host ('Ractiv serial string : {0}' -f $fullSerial) -ForegroundColor Green

    $serialValid = ($fullSerial.Length -eq 10 -and $fullSerial.StartsWith('0101'))
    Write-Host ('Historical serial check (10 chars + 0101 prefix): {0}' -f $(if ($serialValid) { 'PASS' } else { 'WARN' }))

    $cloudKey = ''
    if ($fullSerial.Length -ge 10) {
        $suffix = $fullSerial.Substring(4, 6)
        $cloudKey = $suffix.TrimStart('0')
        if ([string]::IsNullOrEmpty($cloudKey)) { $cloudKey = '0' }
    }

    Write-Host ('Historical cloud key : {0}' -f $(if ($cloudKey) { $cloudKey } else { '<unavailable>' })) -ForegroundColor Green
    Write-Host ''

    if (-not $TryFactoryDownload) {
        Write-Host 'PHASE 1B RESULT: SERIAL_RECOVERED' -ForegroundColor Green
        Write-Host 'No network request was made.'
        Write-Host 'Re-run with -TryFactoryDownload to probe the archived Ractiv calibration paths (JPG/TXT only).'
        exit 0
    }

    if (-not $cloudKey) { throw 'Cannot derive the historical calibration key from this serial.' }

    $outDir = Join-Path $PSScriptRoot (Join-Path 'factory-calibration' $fullSerial)
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    $names = @('0.jpg', '1.jpg', 'stereoCalibData.txt')
    $downloaded = @()

    Write-Host 'Probing historical Ractiv factory calibration CDN...' -ForegroundColor Yellow
    Write-Host ('Output directory: {0}' -f $outDir)

    foreach ($name in $names) {
        $success = $false
        foreach ($scheme in @('https', 'http')) {
            $uri = '{0}://d2i9bzz66ghms6.cloudfront.net/data/{1}/{2}' -f $scheme, $cloudKey, $name
            $dest = Join-Path $outDir $name
            Write-Host ('  GET {0}' -f $uri)
            try {
                Invoke-WebRequest -Uri $uri -OutFile $dest -UseBasicParsing -TimeoutSec 15
                if ((Test-Path $dest) -and (Get-Item $dest).Length -gt 0) {
                    Write-Host ('    FOUND {0} ({1} bytes)' -f $name, (Get-Item $dest).Length) -ForegroundColor Green
                    $success = $true
                    $downloaded += $name
                    break
                }
            } catch {
                Write-Host ('    unavailable: {0}' -f $_.Exception.Message)
                Remove-Item $dest -Force -ErrorAction SilentlyContinue
            }
        }
        if (-not $success) { Write-Host ('    NOT FOUND: {0}' -f $name) -ForegroundColor DarkYellow }
    }

    Write-Host ''
    if ($downloaded.Count -eq 3) {
        Write-Host 'PHASE 1B RESULT: FACTORY_CALIBRATION_RECOVERED' -ForegroundColor Green
        Write-Host 'All three historical factory inputs were recovered. Do not execute anything; these are only JPG/TXT calibration data.'
    } elseif ($downloaded.Count -gt 0) {
        Write-Host ('PHASE 1B RESULT: FACTORY_CALIBRATION_PARTIAL ({0}/3)' -f $downloaded.Count) -ForegroundColor Yellow
    } else {
        Write-Host 'PHASE 1B RESULT: FACTORY_CALIBRATION_CDN_UNAVAILABLE' -ForegroundColor Yellow
        Write-Host 'The serial is still useful; the next fallback is a modern local stereo calibration.'
    }
}
finally {
    if ($handle -ne [IntPtr]::Zero) {
        [TouchPlusFactoryInterop]::EtronDI_Release([ref]$handle)
    }
}
