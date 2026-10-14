$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$output = Join-Path $repo 'build/telemetry-workspace-x64/Release'
$exe = Join-Path $output 'Callayers.exe'
$camera = Join-Path $output 'CallayersCamera.dll'
if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $camera)) { throw 'Build with scripts/build.ps1 -TelemetryBuild first.' }
if (Get-Process -Name Callayers -ErrorAction SilentlyContinue) { throw 'Close the running Callayers instance before starting this build.' }
# Register the same camera identity against this build's consumer-aware DLL.
# Existing browser processes must be restarted once to release their old DLL.
$registration = Start-Process -FilePath "$env:WINDIR/System32/regsvr32.exe" -ArgumentList @('/s', ('"' + $camera + '"')) -Wait -PassThru -WindowStyle Hidden
if ($registration.ExitCode -ne 0) { throw 'Camera registration failed.' }
Start-Process -FilePath $exe -WorkingDirectory $output -WindowStyle Normal
