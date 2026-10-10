$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$installer = Join-Path $projectRoot 'build\WindowsAppRuntimeInstall-x64.exe'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $installer) | Out-Null
if (!(Test-Path -LiteralPath $installer)) {
    Invoke-WebRequest 'https://aka.ms/windowsappsdk/1.6/1.6.250602001/windowsappruntimeinstall-x64.exe' -OutFile $installer
}
$signature = Get-AuthenticodeSignature -FilePath $installer
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike '*O=Microsoft Corporation*') {
    throw 'The runtime installer must have a valid Microsoft signature.'
}
$installation = Start-Process -FilePath $installer -ArgumentList '--quiet' -WindowStyle Hidden -PassThru -Wait
if ($installation.ExitCode -ne 0) { throw "Windows App Runtime installation failed: $($installation.ExitCode)" }
Write-Output 'Windows App Runtime 1.6 support is installed for the WinUI shell.'
