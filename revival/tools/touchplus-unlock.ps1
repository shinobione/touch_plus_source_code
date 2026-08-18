param(
    [switch]$LegacyInit,
    [switch]$DiagnosticsOnly
)

$ErrorActionPreference = 'Stop'

Write-Host 'TouchPlus Revival - Phase 0B Etron control probe' -ForegroundColor Cyan
Write-Host ''

# The recovered Ractiv Windows SDK/DLLs are Win32. Relaunch ourselves under
# 32-bit Windows PowerShell when started from a 64-bit shell.
if ([Environment]::Is64BitProcess) {
    $ps32 = Join-Path $env:WINDIR 'SysWOW64\WindowsPowerShell\v1.0\powershell.exe'
    if (-not (Test-Path $ps32)) {
        throw "32-bit Windows PowerShell was not found at $ps32"
    }

    Write-Host 'Relaunching under 32-bit Windows PowerShell for the legacy Etron DLLs...'
    $args32 = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath)
    if ($LegacyInit) { $args32 += '-LegacyInit' }
    if ($DiagnosticsOnly) { $args32 += '-DiagnosticsOnly' }
    & $ps32 @args32
    exit $LASTEXITCODE
}

Write-Host ('Process architecture: {0}-bit' -f ([IntPtr]::Size * 8))

$dllDir = $PSScriptRoot
$required = @('eSPAEAWBCtrl.dll', 'eSPDI.dll', 'EtLib.dll')
foreach ($name in $required) {
    $path = Join-Path $dllDir $name
    if (-not (Test-Path $path)) {
        throw "Missing $name next to this script. Use the packaged Phase 0B unlock kit."
    }
}

$env:PATH = "$dllDir;$env:PATH"
[Environment]::CurrentDirectory = $dllDir
Set-Location $dllDir

$interop = @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class TouchPlusEtron {
    [DllImport("eSPDI.dll", CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool EtronDI_Init(out IntPtr handle);

    [DllImport("eSPDI.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EtronDI_FindDevice(IntPtr handle);

    [DllImport("eSPDI.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EtronDI_GetDeviceNumber(IntPtr handle);

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
    public static extern int eSPAEAWB_DisableAE();

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_DisableAWB();

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_GetExposureTime(int sensorMode, out float exposureMs);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_SetExposureTime(int sensorMode, float exposureMs);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_GetGlobalGain(int sensorMode, out float gain);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_SetGlobalGain(int sensorMode, float gain);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_SetColorGain(int sensorMode, float r, float g, float b);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_GetGPIOValue(int gpioIndex, out byte value);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_SetGPIOValue(int gpioIndex, byte value);

    [DllImport("eSPAEAWBCtrl.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int eSPAEAWB_GetAccMeterValue(out int x, out int y, out int z);
}
'@

Add-Type -TypeDefinition $interop -Language CSharp

function Show-Ret([string]$name, [int]$code) {
    $state = if ($code -eq 0) { 'OK' } else { 'RET=' + $code }
    Write-Host ('{0,-30} {1}' -f $name, $state)
}

$handle = [IntPtr]::Zero
try {
    $init = [TouchPlusEtron]::EtronDI_Init([ref]$handle)
    Write-Host ('EtronDI_Init                 {0}' -f $init)
    if ($init -and $handle -ne [IntPtr]::Zero) {
        Write-Host ('EtronDI_GetDeviceNumber      {0}' -f [TouchPlusEtron]::EtronDI_GetDeviceNumber($handle))
        Write-Host ('EtronDI_FindDevice           {0}' -f [TouchPlusEtron]::EtronDI_FindDevice($handle))
    }

    $count = 0
    $ret = [TouchPlusEtron]::eSPAEAWB_EnumDevice([ref]$count)
    Show-Ret 'eSPAEAWB_EnumDevice' $ret
    Write-Host ('Etron camera count             {0}' -f $count)

    $touchIndex = -1
    for ($i = 0; $i -lt $count; $i++) {
        $sb = New-Object System.Text.StringBuilder 255
        $nameRet = [TouchPlusEtron]::eSPAEAWB_GetDevicename($i, $sb, 255)
        Write-Host ('  [{0}] {1}  (ret={2})' -f $i, $sb.ToString(), $nameRet)
        if ($touchIndex -lt 0 -and $sb.ToString() -like 'Touch+ Camera*') {
            $touchIndex = $i
        }
    }

    if ($touchIndex -lt 0) {
        throw 'The Etron control DLL did not enumerate a Touch+ Camera.'
    }

    Write-Host ('Selected Touch+ index           {0}' -f $touchIndex) -ForegroundColor Green
    Show-Ret 'eSPAEAWB_SelectDevice' ([TouchPlusEtron]::eSPAEAWB_SelectDevice($touchIndex))
    # Historical Touch+ source selects sensor type 1 / OV7740.
    Show-Ret 'eSPAEAWB_SetSensorType(1)' ([TouchPlusEtron]::eSPAEAWB_SetSensorType(1))

    if (-not $DiagnosticsOnly) {
        $unlock = [TouchPlusEtron]::eSPAEAWB_SWUnlock([UInt16]0x0107)
        Show-Ret 'eSPAEAWB_SWUnlock(0x0107)' $unlock
    } else {
        Write-Host 'Unlock skipped (DiagnosticsOnly).'
    }

    $exp = 0.0
    $gain0 = 0.0
    $gain1 = 0.0
    Show-Ret 'GetExposureTime(both)' ([TouchPlusEtron]::eSPAEAWB_GetExposureTime(2, [ref]$exp))
    Write-Host ('  exposure = {0:N3} ms' -f $exp)
    Show-Ret 'GetGlobalGain(left)' ([TouchPlusEtron]::eSPAEAWB_GetGlobalGain(0, [ref]$gain0))
    Show-Ret 'GetGlobalGain(right)' ([TouchPlusEtron]::eSPAEAWB_GetGlobalGain(1, [ref]$gain1))
    Write-Host ('  gain L/R = {0:N3} / {1:N3}' -f $gain0, $gain1)

    $ax = 0; $ay = 0; $az = 0
    $accRet = [TouchPlusEtron]::eSPAEAWB_GetAccMeterValue([ref]$ax, [ref]$ay, [ref]$az)
    Show-Ret 'GetAccMeterValue' $accRet
    Write-Host ('  accel X/Y/Z = {0} / {1} / {2}' -f $ax, $ay, $az)

    if ($LegacyInit) {
        Write-Host ''
        Write-Host 'Applying the recovered Ractiv camera initializer...' -ForegroundColor Yellow
        Show-Ret 'DisableAE' ([TouchPlusEtron]::eSPAEAWB_DisableAE())
        Show-Ret 'DisableAWB' ([TouchPlusEtron]::eSPAEAWB_DisableAWB())

        $gpio = [byte]0
        $gpioRet = [TouchPlusEtron]::eSPAEAWB_GetGPIOValue(1, [ref]$gpio)
        Show-Ret 'GetGPIOValue(1)' $gpioRet
        if ($gpioRet -eq 0) {
            $gpio = [byte]($gpio -bor 0x08)
            Show-Ret 'LEDs ON / SetGPIOValue' ([TouchPlusEtron]::eSPAEAWB_SetGPIOValue(1, $gpio))
        }

        Show-Ret 'Exposure both = 15 ms' ([TouchPlusEtron]::eSPAEAWB_SetExposureTime(2, [single]15.0))
        Show-Ret 'Global gain left = 1' ([TouchPlusEtron]::eSPAEAWB_SetGlobalGain(0, [single]1.0))
        Show-Ret 'Global gain right = 1' ([TouchPlusEtron]::eSPAEAWB_SetGlobalGain(1, [single]1.0))
        Show-Ret 'Color gain left = 2/1/2' ([TouchPlusEtron]::eSPAEAWB_SetColorGain(0, [single]2.0, [single]1.0, [single]2.0))
        Show-Ret 'Color gain right = 2/1/2' ([TouchPlusEtron]::eSPAEAWB_SetColorGain(1, [single]2.0, [single]1.0, [single]2.0))
    }

    Write-Host ''
    if ($LegacyInit) {
        Write-Host 'PHASE 0B RESULT: unlock + recovered camera initialization attempted.' -ForegroundColor Green
        Write-Host 'Close this window, reopen Windows Camera, select Touch+ Camera, and check whether the stereo image appears.'
    } elseif ($DiagnosticsOnly) {
        Write-Host 'PHASE 0B RESULT: diagnostics completed without sending SWUnlock.' -ForegroundColor Green
    } else {
        Write-Host 'PHASE 0B RESULT: software unlock attempted.' -ForegroundColor Green
        Write-Host 'First reopen Windows Camera and test. If it stays gray, rerun this script with -LegacyInit.'
    }
}
finally {
    if ($handle -ne [IntPtr]::Zero) {
        [TouchPlusEtron]::EtronDI_Release([ref]$handle)
    }
}
