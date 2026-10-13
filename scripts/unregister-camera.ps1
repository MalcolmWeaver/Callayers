$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
$camera = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\x64\Release\CallayersCamera.dll'
if (-not (Test-Path -LiteralPath $camera)) { throw 'The camera DLL is missing. Rebuild before unregistering.' }
$registration = Start-Process -FilePath "$env:WINDIR\System32\regsvr32.exe" -ArgumentList @('/s', '/u', ('"' + $camera + '"')) -Wait -PassThru -WindowStyle Hidden
if ($registration.ExitCode -ne 0) { throw "Camera unregistration failed ($($registration.ExitCode))." }
Write-Host 'The foundation camera was unregistered for this Windows user. Saved cards and previous camera registrations were not changed.'
