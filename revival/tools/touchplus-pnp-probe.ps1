param(
    [switch]$All
)

$ErrorActionPreference = 'Stop'

function Get-PnpPropertyValue {
    param(
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [Parameter(Mandatory = $true)][string]$KeyName
    )

    try {
        return (Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $KeyName -ErrorAction Stop).Data
    }
    catch {
        return $null
    }
}

Write-Host 'TouchPlus Revival - Windows PnP/USB probe' -ForegroundColor Cyan
Write-Host 'No custom driver or executable is used. This script only reads Windows PnP state.'
Write-Host ''

$rows = foreach ($device in (Get-PnpDevice -PresentOnly)) {
    $instanceId = [string]$device.InstanceId
    if ($instanceId -notlike 'USB\*') {
        continue
    }

    $problemCode = Get-PnpPropertyValue -InstanceId $instanceId -KeyName 'DEVPKEY_Device_ProblemCode'
    $hardwareIds = Get-PnpPropertyValue -InstanceId $instanceId -KeyName 'DEVPKEY_Device_HardwareIds'
    $location = Get-PnpPropertyValue -InstanceId $instanceId -KeyName 'DEVPKEY_Device_LocationInfo'
    $parent = Get-PnpPropertyValue -InstanceId $instanceId -KeyName 'DEVPKEY_Device_Parent'

    $hardwareIdText = if ($hardwareIds) { ($hardwareIds -join '; ') } else { '' }
    $haystack = (($instanceId, $device.FriendlyName, $hardwareIdText) -join ' ').ToLowerInvariant()

    $isTouchPlus = $haystack -match 'vid_1e4e&pid_0107' -or $haystack -match 'touch\+ camera'
    $isDescriptorFailure = $haystack -match 'device_descriptor_failure' -or
                           $haystack -match 'usb\\unknown' -or
                           $haystack -match 'vid_0000&pid_0002'
    $isCode43 = [int]$problemCode -eq 43

    if (-not $All -and -not ($isTouchPlus -or $isDescriptorFailure -or $isCode43)) {
        continue
    }

    $kind = if ($isTouchPlus) {
        'TOUCHPLUS'
    }
    elseif ($isDescriptorFailure) {
        'DESCRIPTOR-FAIL'
    }
    elseif ($isCode43) {
        'CODE-43'
    }
    else {
        'USB'
    }

    [pscustomobject]@{
        Kind         = $kind
        Status       = $device.Status
        Class        = $device.Class
        FriendlyName = $device.FriendlyName
        InstanceId   = $instanceId
        ProblemCode  = $problemCode
        HardwareIds  = $hardwareIdText
        Location     = $location
        Parent       = $parent
    }
}

if (-not $rows) {
    Write-Host 'No Touch+ identity, descriptor failure, or USB Code 43 device is currently present.' -ForegroundColor Yellow
    Write-Host 'Run again with -All to list every present USB PnP device.'
    exit 2
}

$rows | Format-List

$touchCount = @($rows | Where-Object Kind -eq 'TOUCHPLUS').Count
$descriptorCount = @($rows | Where-Object Kind -eq 'DESCRIPTOR-FAIL').Count
$code43Count = @($rows | Where-Object { [int]$_.ProblemCode -eq 43 }).Count

Write-Host 'Summary' -ForegroundColor Cyan
Write-Host "  Touch+ VID/PID matches:      $touchCount"
Write-Host "  Descriptor-failure suspects: $descriptorCount"
Write-Host "  USB Code 43 devices:         $code43Count"

if ($touchCount -gt 0) {
    Write-Host 'RESULT: Windows has enumerated a Touch+ identity.' -ForegroundColor Green
    exit 0
}

if ($descriptorCount -gt 0 -or $code43Count -gt 0) {
    Write-Host 'RESULT: USB reaches Windows PnP, but at least one device is failing before/while normal enumeration completes.' -ForegroundColor Yellow
    exit 3
}

exit 2
