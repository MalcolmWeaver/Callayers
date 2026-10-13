param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$DiagnosticsBuild, [switch]$WorkspaceBuild)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json | Select-Object -First 1
if (-not $installation) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$major = ([version]$installation.installationVersion).Major
$generator = if ($major -ge 18) { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
$sdk = Join-Path $env:USERPROFILE '.nuget\packages\microsoft.web.webview2\1.0.2651.64\build\native'
if (-not (Test-Path -LiteralPath (Join-Path $sdk 'include\WebView2.h'))) {
    $deps = Join-Path $repo '.deps'
    New-Item -ItemType Directory -Path $deps -Force | Out-Null
    $package = Join-Path $deps 'webview2-1.0.2651.64.zip'
    $unpacked = Join-Path $deps 'webview2-1.0.2651.64'
    if (-not (Test-Path -LiteralPath (Join-Path $unpacked 'build\native\include\WebView2.h'))) {
        Invoke-WebRequest -Uri 'https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.2651.64/microsoft.web.webview2.1.0.2651.64.nupkg' -OutFile $package
        Expand-Archive -LiteralPath $package -DestinationPath $unpacked -Force
    }
    $sdk = Join-Path $unpacked 'build\native'
}
$winui = Join-Path $env:USERPROFILE '.nuget\packages\microsoft.windowsappsdk\1.6.250602001'
if (-not (Test-Path -LiteralPath (Join-Path $winui 'include\MddBootstrap.h'))) {
    $deps = Join-Path $repo '.deps'
    New-Item -ItemType Directory -Path $deps -Force | Out-Null
    $package = Join-Path $deps 'windowsappsdk-1.6.250602001.zip'
    $winui = Join-Path $deps 'windowsappsdk-1.6.250602001'
    if (-not (Test-Path -LiteralPath (Join-Path $winui 'include\MddBootstrap.h'))) {
        Invoke-WebRequest -Uri 'https://api.nuget.org/v3-flatcontainer/microsoft.windowsappsdk/1.6.250602001/microsoft.windowsappsdk.1.6.250602001.nupkg' -OutFile $package
        Expand-Archive -LiteralPath $package -DestinationPath $winui -Force
    }
}
if ($DiagnosticsBuild -and $WorkspaceBuild) { throw 'Choose one build output.' }
$build = Join-Path $repo $(if ($WorkspaceBuild) { 'build\workspace-x64' } elseif ($DiagnosticsBuild) { 'build\diagnostics-x64' } else { 'build\x64' })
& cmake -S $repo -B $build -G $generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$($installation.installationPath)" "-DWEBVIEW2_ROOT=$sdk" "-DWINDOWSAPPSDK_ROOT=$winui"
if ($LASTEXITCODE -ne 0) { throw 'Build configuration failed.' }
& cmake --build $build --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed. Close Callayers and any calling app holding its camera DLL before rebuilding.' }
Write-Host "Build ready: $build\$Configuration\Callayers.exe"
Write-Host 'This compilation is not a runtime pass. Follow the active feature checklist and AGENTS.md with the user.'
