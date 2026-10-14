param([switch]$ConfigureCi)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$project='callayers';$projectNumber='955736732096'
$config=[IO.File]::ReadAllText((Join-Path $repo 'release/download-config.json'))|ConvertFrom-Json
$bucket=[string]$config.bucket
if($bucket -cne 'callayers-releases-955736732096'){throw 'Review the configured release bucket before provisioning.'}
function Cloud([string[]]$Arguments){$value=&gcloud @Arguments --project=$project --quiet;if($LASTEXITCODE -ne 0){throw 'Google release storage setup failed.'};return $value}
$identity='callayers-release-ci@callayers.iam.gserviceaccount.com'
$pool='callayers-github';$provider='callayers-main'
if($ConfigureCi){
    # Do not run or change the separately approved backend identity setup implicitly.
    $repoInfo=Invoke-RestMethod 'https://api.github.com/repos/MalcolmWeaver/Callayers'
    $providerInfo=(Cloud @('iam','workload-identity-pools','providers','describe',$provider,"--workload-identity-pool=$pool",'--location=global','--format=json'))|ConvertFrom-Json
    $expectedCondition="assertion.repository_id=='$($repoInfo.id)' && assertion.repository_owner_id=='$($repoInfo.owner.id)' && assertion.ref=='refs/heads/main'"
    if($providerInfo.attributeCondition -cne $expectedCondition -or $providerInfo.oidc.issuerUri -cne 'https://token.actions.githubusercontent.com'){throw 'Existing Google provider is not restricted to this repository/main as expected. No release permissions granted.'}
}
$token=(Cloud @('auth','print-access-token')).Trim()
$headers=@{Authorization="Bearer $token"}
$bucketRoute="https://storage.googleapis.com/storage/v1/b/$bucket"
$existing=$null
try{$existing=Invoke-RestMethod -Uri $bucketRoute -Headers $headers}catch{
    if(-not $_.Exception.Response -or [int]$_.Exception.Response.StatusCode -ne 404){throw 'Cannot inspect the release bucket.'}
}
$cors=[IO.File]::ReadAllText((Join-Path $repo 'release/gcs-cors.json'))|ConvertFrom-Json
$settings=@{storageClass='STANDARD';versioning=@{enabled=$false};softDeletePolicy=@{retentionDurationSeconds='0'};
    iamConfiguration=@{uniformBucketLevelAccess=@{enabled=$true};publicAccessPrevention='inherited'};cors=@($cors)}
if($existing){
    if([string]$existing.projectNumber -cne $projectNumber -or $existing.location -ine 'US-CENTRAL1'){throw 'Existing bucket project/location differs; no settings changed.'}
    Invoke-RestMethod -Method Patch -Uri $bucketRoute -Headers $headers -ContentType 'application/json' -Body ($settings|ConvertTo-Json -Depth 8)|Out-Null
}else{
    $settings.name=$bucket;$settings.location='US-CENTRAL1'
    Invoke-RestMethod -Method Post -Uri "https://storage.googleapis.com/storage/v1/b?project=$project" -Headers $headers -ContentType 'application/json' -Body ($settings|ConvertTo-Json -Depth 8)|Out-Null
}
# This dedicated bucket contains public release artifacts only.
Cloud @('storage','buckets','add-iam-policy-binding',"gs://$bucket",'--member=allUsers','--role=roles/storage.objectViewer')|Out-Null
if($ConfigureCi){
    $accounts=Cloud @('iam','service-accounts','list','--format=value(email)')
    if(-not($accounts -contains $identity)){Cloud @('iam','service-accounts','create','callayers-release-ci','--display-name=Callayers release publishing')|Out-Null}
    $principal="principalSet://iam.googleapis.com/projects/$projectNumber/locations/global/workloadIdentityPools/$pool/attribute.repository_id/$($repoInfo.id)"
    Cloud @('iam','service-accounts','add-iam-policy-binding',$identity,"--member=$principal",'--role=roles/iam.workloadIdentityUser')|Out-Null
    Cloud @('storage','buckets','add-iam-policy-binding',"gs://$bucket","--member=serviceAccount:$identity",'--role=roles/storage.objectAdmin')|Out-Null
    $directory=Join-Path $repo '.local';[IO.Directory]::CreateDirectory($directory)|Out-Null
    [IO.File]::WriteAllText((Join-Path $directory 'release-storage-ci.json'),(@{GCP_RELEASE_SERVICE_ACCOUNT=$identity}|ConvertTo-Json))
}
$token='';$headers.Clear()
Write-Host "GCS release bucket prepared: $bucket. No installer was published."
