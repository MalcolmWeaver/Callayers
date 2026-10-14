param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$DiagnosticsBuild, [switch]$WorkspaceBuild, [switch]$UltralightBuild, [switch]$InteractiveBuild, [switch]$GenerationBuild, [switch]$ImageBuild)
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
if (([int]$DiagnosticsBuild.IsPresent + [int]$WorkspaceBuild.IsPresent + [int]$UltralightBuild.IsPresent + [int]$InteractiveBuild.IsPresent + [int]$GenerationBuild.IsPresent + [int]$ImageBuild.IsPresent) -gt 1) { throw 'Choose one build output.' }
$usesUltralight=$UltralightBuild -or $InteractiveBuild -or $GenerationBuild -or $ImageBuild
if ($usesUltralight -and -not (Test-Path "$repo/.deps/ultralight/include/Ultralight/Ultralight.h")) { throw 'Run scripts/build-renderer-test.ps1 first to acquire the pinned Ultralight SDK.' }
if ($usesUltralight -and (Get-Content "$repo/.deps/ultralight/VERSION.txt" -Raw).Trim() -ne '2.0.0-beta.2') { throw 'Expected the pinned Ultralight 2.0.0-beta.2 SDK.' }
$build = Join-Path $repo $(if ($ImageBuild) { 'build\image-workspace-x64' } elseif ($GenerationBuild) { 'build\generation-workspace-x64' } elseif ($InteractiveBuild) { 'build\interactive-workspace-x64' } elseif ($UltralightBuild) { 'build\ultralight-workspace-x64' } elseif ($WorkspaceBuild) { 'build\workspace-x64' } elseif ($DiagnosticsBuild) { 'build\diagnostics-x64' } else { 'build\x64' })
$ultralightOption=if ($usesUltralight) {'ON'} else {'OFF'}
$generationOption=if ($GenerationBuild -or $ImageBuild) {'ON'} else {'OFF'}
$interactiveOption=if ($InteractiveBuild -or $GenerationBuild -or $ImageBuild) {'ON'} else {'OFF'}
$imageOption=if ($ImageBuild) {'ON'} else {'OFF'}
& cmake -S $repo -B $build -G $generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$($installation.installationPath)" "-DWEBVIEW2_ROOT=$sdk" "-DWINDOWSAPPSDK_ROOT=$winui" "-DCALLAYERS_ULTRALIGHT_CPU=$ultralightOption" "-DCALLAYERS_INTERACTIVE=$interactiveOption" "-DCALLAYERS_GENERATION=$generationOption" "-DCALLAYERS_IMAGES=$imageOption"
if ($LASTEXITCODE -ne 0) { throw 'Build configuration failed.' }
& cmake --build $build --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed. Close Callayers and any calling app holding its camera DLL before rebuilding.' }
if ($usesUltralight) {
    $backendFile=Join-Path $repo 'build/workspace-x64/Release/service-url.txt'
    if (Test-Path $backendFile) { Copy-Item -LiteralPath $backendFile -Destination "$build/$Configuration/service-url.txt" }
}
Write-Host "Build ready: $build\$Configuration\Callayers.exe"
Write-Host 'This compilation is not a runtime pass. Follow the active feature checklist and AGENTS.md with the user.'
