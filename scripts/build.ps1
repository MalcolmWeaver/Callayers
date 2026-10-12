param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$SkipTests,
    [string]$ServiceUrl = '', [string]$AppName = 'Callayers')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$appSdk = & "$PSScriptRoot\prepare-winui.ps1"
cmake -S $projectRoot -B "$projectRoot\build" -G 'Visual Studio 17 2022' -A x64 "-DCALLAYERS_WINAPPSDK=$appSdk" "-DCALLAYERS_SERVICE_URL=$ServiceUrl" "-DCALLAYERS_APP_NAME=$AppName"
if ($LASTEXITCODE -ne 0) { throw 'Configuration failed' }
cmake --build "$projectRoot\build" --config $Configuration -- /m:1 /nr:false
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if ($SkipTests) { return }
ctest --test-dir "$projectRoot\build" -C $Configuration --output-on-failure --timeout 20
if ($LASTEXITCODE -ne 0) { throw 'Verification failed' }
