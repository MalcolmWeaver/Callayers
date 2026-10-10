$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'
function Get-BuildPackage([string]$Name, [string]$Version) {
    $cached = Join-Path $env:USERPROFILE ".nuget\packages\$Name\$Version"
    if (Test-Path -LiteralPath $cached) { return $cached }
    $destination = Join-Path $buildRoot "packages\$Name\$Version"
    if (!(Test-Path -LiteralPath (Join-Path $destination "$Name.nuspec"))) {
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        $archive = Join-Path $buildRoot "$Name.$Version.zip"
        Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/$Name/$Version/$Name.$Version.nupkg" -OutFile $archive
        Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
    }
    return $destination
}
$appSdk = Get-BuildPackage 'microsoft.windowsappsdk' '1.6.250602001'
$webSdk = Get-BuildPackage 'microsoft.web.webview2' '1.0.2651.64'
$kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$generator = Get-ChildItem "$kitsRoot\bin\*\x64\cppwinrt.exe" | Sort-Object FullName -Descending | Select-Object -First 1
if (!$generator) { throw 'Install the Windows SDK C++/WinRT tools.' }
$generated = Join-Path $buildRoot 'generated'
$stamp = Join-Path $generated 'callayers-sdk.stamp'
if (!(Test-Path -LiteralPath $stamp)) {
    & $generator.FullName -input sdk -input "$appSdk\lib\uap10.0" -input "$appSdk\lib\uap10.0.18362" -input "$webSdk\lib\Microsoft.Web.WebView2.Core.winmd" -output $generated
    if ($LASTEXITCODE -ne 0) { throw 'WinUI C++ projection generation failed.' }
    Set-Content -LiteralPath $stamp -Value 'WindowsAppSDK 1.6.250602001 / WebView2 1.0.2651.64'
}
return $appSdk
