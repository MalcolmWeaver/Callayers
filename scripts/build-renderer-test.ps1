[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$sdk=Join-Path $repo '.deps/ultralight'
& "$PSScriptRoot/acquire-ultralight.ps1"
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json | Select-Object -First 1
if (-not $installation) { throw 'Visual Studio C++ Build Tools required.' }
$generator=if (([version]$installation.installationVersion).Major -ge 18) {'Visual Studio 18 2026'} else {'Visual Studio 17 2022'}
$build=Join-Path $repo 'build/renderer-test-v2-x64'
& cmake -S "$repo/renderer-test" -B $build -G $generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$($installation.installationPath)"
if ($LASTEXITCODE -ne 0) { throw 'Renderer test configuration failed.' }
& cmake --build $build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Renderer test compilation failed.' }
Write-Host "Ready: $build/Release/CallayersUltralightTest.exe"
