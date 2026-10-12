# Run elevated. Installs only the Callayers development COM source, not a kernel driver.
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script in an Administrator PowerShell window to register the camera source.'
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceDll = Join-Path $projectRoot "build\$Configuration\CallayersCamera.dll"
if (-not (Test-Path -LiteralPath $sourceDll)) { throw 'Build Callayers before registering it.' }
$installDirectory = Join-Path $env:ProgramFiles 'Callayers Development'
New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
$buildHash = (Get-FileHash -LiteralPath $sourceDll -Algorithm SHA256).Hash.Substring(0,12)
$installedDll = Join-Path $installDirectory "CallayersCamera-$buildHash.dll"
if (-not (Test-Path -LiteralPath $installedDll)) { Copy-Item -LiteralPath $sourceDll -Destination $installedDll }
$classKey = 'HKLM:\SOFTWARE\Classes\CLSID\{A487A33B-6EC3-43F1-A894-CA2D937619F0}'
New-Item -Path "$classKey\InprocServer32" -Force | Out-Null
Set-Item -LiteralPath $classKey -Value 'Callayers development camera source'
Set-Item -LiteralPath "$classKey\InprocServer32" -Value $installedDll
New-ItemProperty -LiteralPath "$classKey\InprocServer32" -Name ThreadingModel -Value Both -PropertyType String -Force | Out-Null
Write-Output "Registered $installedDll. Run Callayers as your normal user ; camera output starts automatically."
