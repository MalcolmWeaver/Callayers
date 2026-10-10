param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$app = Join-Path $projectRoot "build\$Configuration\Callayers.exe"
# A separate preview exercises WinUI initialization, its controls, message loop,
# frame painting and orderly shutdown without claiming the camera producer pipe.
$preview = Start-Process -FilePath $app -ArgumentList '--shell-only','--smoke-test' -WindowStyle Hidden -PassThru
if (!$preview.WaitForExit(15000)) { throw "Shell smoke test did not finish; inspect process $($preview.Id)." }
if ($preview.ExitCode -ne 0) { throw "Shell smoke test failed: $($preview.ExitCode). See build\$Configuration\Callayers-startup.log." }
Write-Output 'PASS: WinUI runtime initialization, control creation, event loop and clean shutdown.'
