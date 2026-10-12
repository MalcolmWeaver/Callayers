param([Parameter(Mandatory)][ValidatePattern('^[a-z][a-z0-9-]{4,61}[a-z0-9]$')][string]$ProjectId)
$ErrorActionPreference = 'Stop'
function Invoke-Cloud {
    # Windows PowerShell can turn gcloud.ps1's native stderr into a terminating
    # error even for warnings. Let gcloud finish, then use its exit code.
    $ErrorActionPreference = 'Continue'
    $PSNativeCommandUseErrorActionPreference = $false
    & gcloud @args
    if ($LASTEXITCODE -ne 0) { throw 'Google Cloud setup command failed. See the error above.' }
}
# Run after creating this project, linking billing, and enabling Firebase in its console.
Invoke-Cloud services enable run.googleapis.com cloudbuild.googleapis.com artifactregistry.googleapis.com `
    firestore.googleapis.com identitytoolkit.googleapis.com secretmanager.googleapis.com firebasehosting.googleapis.com --project $ProjectId
$identity = "callayers-api@$ProjectId.iam.gserviceaccount.com"
$existingIdentities = @(Invoke-Cloud iam service-accounts list --project $ProjectId --format 'value(email)')
if ($existingIdentities -notcontains $identity) { Invoke-Cloud iam service-accounts create callayers-api --display-name 'Callayers backend' --project $ProjectId }
foreach ($role in @('roles/datastore.user', 'roles/firebaseauth.viewer')) {
    Invoke-Cloud projects add-iam-policy-binding $ProjectId --member "serviceAccount:$identity" --role $role --condition None
}
$existingRepositories = @(Invoke-Cloud artifacts repositories list --project $ProjectId --location us-central1 --format 'value(name)' | ForEach-Object { ($_ -split '/')[-1] })
if ($existingRepositories -notcontains 'callayers') { Invoke-Cloud artifacts repositories create callayers --project $ProjectId --location us-central1 --repository-format docker }
$existingSecrets = @(Invoke-Cloud secrets list --project $ProjectId --format 'value(name)' | ForEach-Object { ($_ -split '/')[-1] })
foreach ($secret in @('callayers-openai', 'callayers-posthog')) {
    if ($existingSecrets -notcontains $secret) { Invoke-Cloud secrets create $secret --project $ProjectId --replication-policy automatic }
    Invoke-Cloud secrets add-iam-policy-binding $secret --project $ProjectId --member "serviceAccount:$identity" --role roles/secretmanager.secretAccessor
}
Write-Host 'Resource setup completed. Add secret values in the Google Cloud console; create the Firestore database and enable Google sign-in in Firebase.'
