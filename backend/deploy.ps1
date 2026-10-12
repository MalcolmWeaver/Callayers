param(
    [Parameter(Mandatory)][ValidatePattern('^[a-z][a-z0-9-]{4,61}[a-z0-9]$')][string]$ProjectId,
    [string]$EnvironmentFile = "$PSScriptRoot\env.local.yaml"
)
$ErrorActionPreference = 'Stop'
function Invoke-Cloud {
    $ErrorActionPreference = 'Continue'
    $PSNativeCommandUseErrorActionPreference = $false
    & gcloud @args
    if ($LASTEXITCODE -ne 0) { throw 'Google Cloud deployment command failed. See the error above.' }
}
if (-not (Test-Path -LiteralPath $EnvironmentFile)) { throw 'Copy env.example.yaml to env.local.yaml and fill in the project configuration first.' }
$deploymentConfig = Get-Content -LiteralPath $EnvironmentFile -Raw
if ($deploymentConfig -notmatch ('GOOGLE_CLOUD_PROJECT:\s*"?' + [regex]::Escape($ProjectId) + '"?\s*(\r?\n|$)')) { throw 'Environment configuration must name the target project.' }
Push-Location $PSScriptRoot
try {
    $serviceIdentity = "callayers-api@$ProjectId.iam.gserviceaccount.com"
    $image = "us-central1-docker.pkg.dev/$ProjectId/callayers/api:$(Get-Date -Format yyyyMMddHHmmss)"
    Invoke-Cloud builds submit . --project $ProjectId --tag $image
    Invoke-Cloud run deploy callayers-api --project $ProjectId --region us-central1 --image $image `
        --service-account $serviceIdentity --allow-unauthenticated --timeout 180 --concurrency 8 `
        --min-instances 0 --max-instances 2 --memory 512Mi --cpu 1 `
        --env-vars-file $EnvironmentFile --set-secrets 'OPENAI_API_KEY=callayers-openai:latest,POSTHOG_KEY=callayers-posthog:latest'
    npm.cmd ci --no-audit --no-fund
    if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }
    npm.cmd run build
    if ($LASTEXITCODE -ne 0) { throw 'Backend/web build failed.' }
    npx.cmd firebase-tools deploy --project $ProjectId --only hosting,firestore
    if ($LASTEXITCODE -ne 0) { throw 'Firebase deployment failed.' }
    Write-Host 'Deployment commands completed. No verification requests were run.'
} finally { Pop-Location }
