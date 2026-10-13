param(
    [Parameter(Mandatory)][ValidatePattern('^[a-z][a-z0-9-]{4,61}[a-z0-9]$')][string]$ProjectId,
    [string]$EnvironmentFile = "$PSScriptRoot\env.local.yaml",
    [switch]$DeployFirestore
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
# Preserve an explicitly chosen origin so it matches the desktop build URL.
$configuredOrigin = ''
if ($deploymentConfig -match '(?m)^PUBLIC_ORIGIN:\s*["'']?(https://[A-Za-z0-9.-]+(?::[0-9]+)?)/?["'']?\s*(?:#.*)?$') {
    $configuredOrigin = $Matches[1]
} elseif ($deploymentConfig -match '(?m)^PUBLIC_ORIGIN:') {
    throw 'PUBLIC_ORIGIN must be an HTTPS origin without a path, query, or fragment.'
}
$resolvedEnvironmentFile = $null
Push-Location $PSScriptRoot
try {
    $serviceOrigin = $configuredOrigin
    if (-not $serviceOrigin) {
        # Listing distinguishes a missing service from an authentication/permission failure.
        $serviceOrigin = (Invoke-Cloud run services list --project $ProjectId --region us-central1 `
            --filter 'metadata.name=callayers-api' --format 'value(status.url)' | Out-String).Trim()
        if (-not $serviceOrigin) {
            $projectNumber = (Invoke-Cloud projects describe $ProjectId --format 'value(projectNumber)' | Out-String).Trim()
            if ($projectNumber -notmatch '^\d+$') { throw 'Could not determine the Google Cloud project number.' }
            $serviceOrigin = "https://callayers-api-$projectNumber.us-central1.run.app"
        }
    }
    if ($serviceOrigin -notmatch '^https://[A-Za-z0-9.-]+(?::[0-9]+)?$') { throw 'Could not determine the Cloud Run service origin.' }
    # Supply the origin with the initial rollout, without editing the user's config.
    $deploymentConfig = [regex]::Replace($deploymentConfig, '(?m)^PUBLIC_ORIGIN:[^\r\n]*', '')
    $resolvedEnvironmentFile = [System.IO.Path]::GetTempFileName()
    [System.IO.File]::WriteAllText($resolvedEnvironmentFile, ($deploymentConfig.TrimEnd() + "`nPUBLIC_ORIGIN: `"$serviceOrigin`"`n"))
    $serviceIdentity = "callayers-api@$ProjectId.iam.gserviceaccount.com"
    $image = "us-central1-docker.pkg.dev/$ProjectId/callayers/api:$([guid]::NewGuid().ToString('N'))"
    Write-Host 'Building backend (reusing cached layers when available)...'
    Invoke-Cloud builds submit . --project $ProjectId --config cloudbuild.yaml --substitutions "_IMAGE=$image"
    Write-Host 'Deploying Cloud Run...'
    Invoke-Cloud run deploy callayers-api --project $ProjectId --region us-central1 --image $image `
        --service-account $serviceIdentity --no-invoker-iam-check --timeout 180 --concurrency 8 `
        --min-instances 0 --max-instances 2 --memory 512Mi --cpu 1 `
        --env-vars-file $resolvedEnvironmentFile --set-secrets 'OPENAI_API_KEY=callayers-openai:latest,POSTHOG_KEY=callayers-posthog:latest'
    if ($DeployFirestore) {
        Write-Host 'Deploying Firestore rules and indexes...'
        npx.cmd firebase-tools deploy --project $ProjectId --only firestore
        if ($LASTEXITCODE -ne 0) { throw 'Firestore deployment failed.' }
    } else {
        Write-Host 'Firestore skipped. Use -DeployFirestore for initial setup or rules/index changes.'
    }
    Write-Host "Service URL: $serviceOrigin"
    Write-Host "Add $(([uri]$serviceOrigin).Host) to Firebase Authentication authorized domains."
    Write-Host 'Build the desktop app with this URL using -ServiceUrl.'
    Write-Host 'Deployment commands completed. No verification requests were run.'
} finally {
    if ($resolvedEnvironmentFile) { Remove-Item -LiteralPath $resolvedEnvironmentFile -Force }
    Pop-Location
}
