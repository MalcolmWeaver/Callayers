# First run build\Release\CameraProbe.exe --remove as the user who created the camera.
# Then run this script elevated. Installed DLL is retained if Windows has it loaded.
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run in Administrator PowerShell after removing the current-user camera with CameraProbe --remove.'
}
$classKey = 'HKLM:\SOFTWARE\Classes\CLSID\{A487A33B-6EC3-43F1-A894-CA2D937619F0}'
if (Test-Path -LiteralPath "$classKey\InprocServer32") { Remove-Item -LiteralPath "$classKey\InprocServer32" }
if (Test-Path -LiteralPath $classKey) { Remove-Item -LiteralPath $classKey }
$installDirectory = Join-Path $env:ProgramFiles 'Callayers Development'
if (Test-Path -LiteralPath $installDirectory) {
    Get-ChildItem -LiteralPath $installDirectory -Filter 'CallayersCamera*.dll' -File | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
}
Write-Output 'Removed Callayers development source registration.'
