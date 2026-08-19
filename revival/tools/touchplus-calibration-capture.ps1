param()

Write-Host 'TouchPlus Revival - Phase 1B.2a' -ForegroundColor Cyan
Write-Host ''
Write-Host 'This one-shot PowerShell calibration helper is DEPRECATED.' -ForegroundColor Yellow
Write-Host 'Physical smoke showed that repeatedly reopening the Touch+ can produce gray captures.' -ForegroundColor Yellow
Write-Host ''
Write-Host 'Use the persistent live capture executable instead:' -ForegroundColor Green
Write-Host '  .\touchplus_calibration_capture.exe --pairs 3'
Write-Host ''
Write-Host 'The live tool unlocks once, keeps LEFT/RIGHT visible continuously, and saves a synchronized pair with SPACE.'
exit 2
