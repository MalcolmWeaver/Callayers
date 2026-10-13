$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell for this x64 development build.' }
$repo = Split-Path $PSScriptRoot -Parent
$output = Join-Path $repo 'build\x64\Release'
$exe = Join-Path $output 'Callayers.exe'
$camera = Join-Path $output 'CallayersCamera.dll'
if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $camera)) { throw 'Build the app first with scripts/build.ps1.' }
# The DLL registers only its own new identity under this Windows user's HKCU.
# No elevation, old-camera removal, browser launch, or saved-card changes.
$registration = Start-Process -FilePath "$env:WINDIR\System32\regsvr32.exe" -ArgumentList @('/s', ('"' + $camera + '"')) -Wait -PassThru -WindowStyle Hidden
if ($registration.ExitCode -ne 0) { throw "Camera registration failed ($($registration.ExitCode))." }
Start-Process -FilePath $exe -WorkingDirectory $output -WindowStyle Normal
