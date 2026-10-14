$ErrorActionPreference='Stop'
$project='callayers';$pool='callayers-github';$provider='callayers-main'
$deployer='callayers-v2-ci@callayers.iam.gserviceaccount.com'
function Cloud([string[]]$Arguments){$value=&gcloud @Arguments --project=$project --quiet;if($LASTEXITCODE -ne 0){throw 'Google CI setup failed.'};return $value}
$repoInfo=Invoke-RestMethod 'https://api.github.com/repos/MalcolmWeaver/Callayers'
$number=(Cloud @('projects','describe',$project,'--format=value(projectNumber)')).Trim()
Cloud @('services','enable','iam.googleapis.com','iamcredentials.googleapis.com','sts.googleapis.com')|Out-Null
$pools=Cloud @('iam','workload-identity-pools','list','--location=global','--format=value(name)')
if(-not($pools -match "/$pool$")){Cloud @('iam','workload-identity-pools','create',$pool,'--location=global','--display-name=Callayers GitHub CI')|Out-Null}
$providers=Cloud @('iam','workload-identity-pools','providers','list',"--workload-identity-pool=$pool",'--location=global','--format=value(name)')
$condition="assertion.repository_id=='$($repoInfo.id)' && assertion.repository_owner_id=='$($repoInfo.owner.id)' && assertion.ref=='refs/heads/main'"
if(-not($providers -match "/$provider$")){
 Cloud @('iam','workload-identity-pools','providers','create-oidc',$provider,"--workload-identity-pool=$pool",'--location=global','--issuer-uri=https://token.actions.githubusercontent.com','--attribute-mapping=google.subject=assertion.sub,attribute.repository_id=assertion.repository_id',"--attribute-condition=$condition")|Out-Null
}else{
 Cloud @('iam','workload-identity-pools','providers','update-oidc',$provider,"--workload-identity-pool=$pool",'--location=global',"--attribute-condition=$condition")|Out-Null
}
$accounts=Cloud @('iam','service-accounts','list','--format=value(email)')
if(-not($accounts -contains $deployer)){Cloud @('iam','service-accounts','create','callayers-v2-ci','--display-name=Callayers GitHub deployment')|Out-Null}
$principal="principalSet://iam.googleapis.com/projects/$number/locations/global/workloadIdentityPools/$pool/attribute.repository_id/$($repoInfo.id)"
Cloud @('iam','service-accounts','add-iam-policy-binding',$deployer,"--member=$principal",'--role=roles/iam.workloadIdentityUser')|Out-Null
foreach($role in @('roles/run.sourceDeveloper','roles/serviceusage.serviceUsageConsumer')){Cloud @('projects','add-iam-policy-binding',$project,"--member=serviceAccount:$deployer","--role=$role",'--condition=None')|Out-Null}
$runtime=(Cloud @('run','services','describe','callayers-v2-api','--region=us-central1','--format=value(spec.template.spec.serviceAccountName)')).Trim()
if(-not $runtime){throw 'Could not determine existing service identity.'}
foreach($identity in @($runtime,'callayers-v2-build@callayers.iam.gserviceaccount.com')|Select-Object -Unique){Cloud @('iam','service-accounts','add-iam-policy-binding',$identity,"--member=serviceAccount:$deployer",'--role=roles/iam.serviceAccountUser')|Out-Null}
$result=@{GCP_WORKLOAD_IDENTITY_PROVIDER="projects/$number/locations/global/workloadIdentityPools/$pool/providers/$provider";GCP_DEPLOY_SERVICE_ACCOUNT=$deployer}
$directory=Join-Path (Split-Path $PSScriptRoot -Parent) '.local';[IO.Directory]::CreateDirectory($directory)|Out-Null
[IO.File]::WriteAllText((Join-Path $directory 'backend-ci.json'),($result|ConvertTo-Json))
Write-Host 'Backend CI identity configured for this repository on main. Public GitHub variable values saved in .local/backend-ci.json. Existing runtime configuration was not changed.'
