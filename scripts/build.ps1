param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
cmake -S $projectRoot -B "$projectRoot\build" -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Configuration failed' }
cmake --build "$projectRoot\build" --config $Configuration -- /m:1 /nr:false
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir "$projectRoot\build" -C $Configuration --output-on-failure --timeout 20
if ($LASTEXITCODE -ne 0) { throw 'Verification failed' }
