[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo 'backend-v2'
$project = 'callayers'
$region = 'us-central1'
$service = 'callayers-v2-api'
$buildIdentity = "callayers-v2-build@$project.iam.gserviceaccount.com"

function Cloud([string[]]$Arguments) {
    $result = & gcloud @Arguments --project=$project --quiet
    if ($LASTEXITCODE -ne 0) { throw 'Google Cloud operation failed; deployment stopped.' }
    return $result
}
Push-Location $source
try {
    & npm ci --ignore-scripts --no-audit --no-fund
    if ($LASTEXITCODE -ne 0) { throw 'Backend dependency installation failed.' }
    & npm test
    if ($LASTEXITCODE -ne 0) { throw 'Backend tests failed; deployment stopped.' }
} finally { Pop-Location }

# Publish source to the already-configured service. Runtime settings, secrets,
# access, capacity and database policies are managed separately from deployment.
Cloud -Arguments @('run','deploy',$service,"--source=$source","--region=$region",
    "--build-service-account=projects/$project/serviceAccounts/$buildIdentity") | Out-Null
$url = (Cloud -Arguments @('run','services','describe',$service,"--region=$region",'--format=value(status.url)')).Trim()
$health = Invoke-RestMethod -Uri "$url/health"
$config = Invoke-RestMethod -Uri "$url/client-config"
if ($health.service -ne $service -or $health.version -ne 2 -or $config.firebaseProjectId -ne $project -or $config.version -ne 2) { throw 'V2 endpoint validation failed.' }
if($health.generationReady){
    try { Invoke-RestMethod -Uri "$url/generate" -Method Post -ContentType 'application/json' -Body '{}' | Out-Null; throw 'Generation unexpectedly accepted an unsigned request.' }
    catch { if (-not $_.Exception.Response -or [int]$_.Exception.Response.StatusCode -ne 401) { throw 'Generation access validation failed.' } }
}
try { Invoke-RestMethod -Uri "$url/account" | Out-Null; throw 'Account endpoint unexpectedly accepted a request without sign-in.' }
catch { if (-not $_.Exception.Response -or [int]$_.Exception.Response.StatusCode -ne 401) { throw 'Account endpoint access validation failed.' } }
$output = Join-Path $repo 'build/workspace-x64/Release'
if (-not (Test-Path -LiteralPath $output)) { throw 'Build the workspace desktop first; the backend deployed successfully.' }
[IO.File]::WriteAllText((Join-Path $output 'service-url.txt'), $url, [Text.UTF8Encoding]::new($false))
Write-Host "V2 service ready: $url"
Write-Host "Generation ready: $($health.generationReady)"
Write-Host 'Desktop address configured. Runtime verification remains user-run.'
