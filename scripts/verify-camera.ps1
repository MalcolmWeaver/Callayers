param([switch]$SkipWebcam)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$app = Join-Path $projectRoot 'build\Release\Callayers.exe'
$probe = Join-Path $projectRoot 'build\Release\CameraProbe.exe'
$logDirectory = Join-Path $projectRoot 'logs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

function Test-LiveProducer([string]$Arguments) {
    $producer = Start-Process -FilePath $app -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    try {
        Start-Sleep -Seconds 2
        & $probe --camera 120 --expect-moving
        if ($LASTEXITCODE -ne 0) { throw 'Registered camera did not deliver live frames.' }
        $producer.WaitForExit()
        if ($producer.ExitCode -ne 0) { throw "Producer failed: $($producer.ExitCode)" }
    } finally {
        if (-not $producer.HasExited) { Stop-Process -Id $producer.Id }
    }
}

Test-LiveProducer '--headless --publish --seconds 8'
& $probe --camera 30 --expect-idle
if ($LASTEXITCODE -ne 0) { throw 'Idle fallback failed.' }
if (-not $SkipWebcam) { Test-LiveProducer '--headless --webcam --publish --seconds 8' }

# Keep one Windows camera reader connected across producer close and restart.
$first = Start-Process -FilePath $app -ArgumentList '--headless --publish --seconds 5' -WindowStyle Hidden -PassThru
$second = $null
$reader = $null
try {
    Start-Sleep -Seconds 1
    $reader = Start-Process -FilePath $probe -ArgumentList '--camera 420 --expect-recovery' -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logDirectory 'recovery.txt') -RedirectStandardError (Join-Path $logDirectory 'recovery-error.txt')
    $first.WaitForExit()
    if ($first.ExitCode -ne 0) { throw 'Initial producer failed.' }
    Start-Sleep -Seconds 2
    $second = Start-Process -FilePath $app -ArgumentList '--headless --seconds 10' -WindowStyle Hidden -PassThru
    if (-not $reader.WaitForExit(20000)) { throw 'Recovery reader timed out.' }
    Get-Content -LiteralPath (Join-Path $logDirectory 'recovery.txt')
    if ($reader.ExitCode -ne 0) {
        Get-Content -LiteralPath (Join-Path $logDirectory 'recovery-error.txt')
        throw 'Same-reader recovery failed.'
    }
} finally {
    foreach ($process in @($first,$second,$reader)) {
        if ($null -ne $process -and -not $process.HasExited) { Stop-Process -Id $process.Id }
    }
}
Write-Output 'PASS: registered live output, idle fallback, and same-reader recovery. Complete the second-device meeting check manually.'
