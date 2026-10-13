param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$SkipTests,
    [string]$ServiceUrl = '', [string]$AppName = 'Callayers')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$serviceArguments = @()
if ($PSBoundParameters.ContainsKey('ServiceUrl')) {
    # An explicitly empty value requests a local developer build.
    $serviceArguments = @("-DCALLAYERS_SERVICE_URL=$ServiceUrl")
} else {
    $cachePath = Join-Path $projectRoot 'build\CMakeCache.txt'
    $cachedCloud = (Test-Path $cachePath) -and (Select-String -LiteralPath $cachePath -Pattern '^CALLAYERS_SERVICE_URL:STRING=https://' -Quiet)
    if (-not $cachedCloud) {
        $environmentPath = Join-Path $projectRoot 'backend\env.local.yaml'
        if (Test-Path $environmentPath) {
            $originLine = Get-Content -LiteralPath $environmentPath | Where-Object { $_ -match '^PUBLIC_ORIGIN:' } | Select-Object -First 1
            if ($originLine -match '^PUBLIC_ORIGIN:\s*["'']?(https://[A-Za-z0-9.-]+(?::[0-9]+)?)/?["'']?\s*(?:#.*)?$') {
                $serviceArguments = @("-DCALLAYERS_SERVICE_URL=$($Matches[1])")
            }
        }
    }
}
$appSdk = & "$PSScriptRoot\prepare-winui.ps1"
cmake -S $projectRoot -B "$projectRoot\build" -G 'Visual Studio 17 2022' -A x64 "-DCALLAYERS_WINAPPSDK=$appSdk" @serviceArguments "-DCALLAYERS_APP_NAME=$AppName"
if ($LASTEXITCODE -ne 0) { throw 'Configuration failed' }
cmake --build "$projectRoot\build" --config $Configuration -- /m:1 /nr:false
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if ($SkipTests) { return }
ctest --test-dir "$projectRoot\build" -C $Configuration --output-on-failure --timeout 20
if ($LASTEXITCODE -ne 0) { throw 'Verification failed' }
