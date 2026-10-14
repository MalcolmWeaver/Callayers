[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$sdk=Join-Path $repo '.deps/ultralight'
$sdkVersion='2.0.0-beta.2'
$sdkHash='e17b3ca2f3c2063f1d14971e7ef756f28e71f2d426a91d2e21c5236d2dc67e6d'
if (-not (Test-Path "$sdk/include/Ultralight/Ultralight.h")) {
    New-Item -ItemType Directory -Force $sdk | Out-Null
    $info=Invoke-RestMethod "https://ultralig.ht/api/v1/sdk/download?platform=windows-x64&version=$sdkVersion&format=json"
    Invoke-WebRequest $info.url -OutFile "$sdk/sdk.7z"
    if ((Get-FileHash "$sdk/sdk.7z").Hash -ine $sdkHash) { throw 'Ultralight SDK checksum mismatch.' }
    & tar -xf "$sdk/sdk.7z" -C $sdk
    if ($LASTEXITCODE -ne 0) { throw 'SDK extraction failed.' }
}
if ((Get-Content "$sdk/VERSION.txt" -Raw).Trim() -ne $sdkVersion) { throw "Expected Ultralight $sdkVersion in $sdk." }
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json | Select-Object -First 1
if (-not $installation) { throw 'Visual Studio C++ Build Tools required.' }
$generator=if (([version]$installation.installationVersion).Major -ge 18) {'Visual Studio 18 2026'} else {'Visual Studio 17 2022'}
$build=Join-Path $repo 'build/renderer-test-v2-x64'
& cmake -S "$repo/renderer-test" -B $build -G $generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$($installation.installationPath)"
if ($LASTEXITCODE -ne 0) { throw 'Renderer test configuration failed.' }
& cmake --build $build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Renderer test compilation failed.' }
Write-Host "Ready: $build/Release/CallayersUltralightTest.exe and CallayersWebViewTest.exe"
