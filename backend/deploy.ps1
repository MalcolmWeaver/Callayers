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
# Fail before uploading/building when the example client key has not been replaced.
if ($deploymentConfig -notmatch '(?m)^FIREBASE_WEB_API_KEY:\s*["'']?(AIza[A-Za-z0-9_-]{35})["'']?\s*(?:#.*)?$') {
    throw 'Set FIREBASE_WEB_API_KEY in env.local.yaml to the apiKey from Firebase Project settings > Your apps. The example placeholder is not a working key.'
}
Push-Location $PSScriptRoot
try {
    $serviceIdentity = "callayers-api@$ProjectId.iam.gserviceaccount.com"
    $image = "us-central1-docker.pkg.dev/$ProjectId/callayers/api:$(Get-Date -Format yyyyMMddHHmmss)"
    Invoke-Cloud builds submit . --project $ProjectId --tag $image
    Invoke-Cloud run deploy callayers-api --project $ProjectId --region us-central1 --image $image `
        --service-account $serviceIdentity --no-invoker-iam-check --timeout 180 --concurrency 8 `
        --min-instances 0 --max-instances 2 --memory 512Mi --cpu 1 `
        --env-vars-file $EnvironmentFile --set-secrets 'OPENAI_API_KEY=callayers-openai:latest,POSTHOG_KEY=callayers-posthog:latest'
    $serviceOrigin = (Invoke-Cloud run services describe callayers-api --project $ProjectId --region us-central1 --format 'value(status.url)' | Out-String).Trim()
    if ($serviceOrigin -notmatch '^https://[^/]+$') { throw 'Could not determine the Cloud Run service origin.' }
    Invoke-Cloud run services update callayers-api --project $ProjectId --region us-central1 --update-env-vars "PUBLIC_ORIGIN=$serviceOrigin"
    npx.cmd firebase-tools deploy --project $ProjectId --only firestore
    if ($LASTEXITCODE -ne 0) { throw 'Firestore deployment failed.' }
    Write-Host "Service URL: $serviceOrigin"
    Write-Host "Add $(([uri]$serviceOrigin).Host) to Firebase Authentication authorized domains."
    Write-Host 'Build the desktop app with this URL using -ServiceUrl.'
    Write-Host 'Deployment commands completed. No verification requests were run.'
} finally { Pop-Location }
