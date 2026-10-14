[CmdletBinding()]
param([string]$OAuthJsonPath, [switch]$BuildOnly, [switch]$GenerationOnly, [switch]$TelemetryOnly, [string]$PosthogHost, [string]$PosthogTokenPath)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo 'backend-v2'
$project = 'callayers'
$region = 'us-central1'
$service = 'callayers-v2-api'
$database = 'callayers-v2'
$identity = "$service@$project.iam.gserviceaccount.com"
$buildIdentity = "callayers-v2-build@$project.iam.gserviceaccount.com"
$oauthSecret = 'callayers-v2-desktop-oauth'

function Cloud([string[]]$Arguments) {
    $result = & gcloud @Arguments --project=$project --quiet
    if ($LASTEXITCODE -ne 0) { throw 'Google Cloud operation failed; setup stopped.' }
    return $result
}
function RulesRequest($Method, $Uri, $Body, $Headers) {
    try { return Invoke-RestMethod -Method $Method -Uri $Uri -Headers $Headers -ContentType 'application/json' -Body ($Body | ConvertTo-Json -Depth 10 -Compress) }
    catch { throw 'Could not publish the v2 database access rules; deployment stopped.' }
}

if (([int]$BuildOnly.IsPresent + [int]$GenerationOnly.IsPresent + [int]$TelemetryOnly.IsPresent) -gt 1) { throw 'Choose one setup mode.' }
if ($TelemetryOnly) {
    if ($PosthogHost -notin @('https://us.i.posthog.com','https://eu.i.posthog.com') -or -not $PosthogTokenPath) { throw 'Specify the selected project ingestion host and token file.' }
    $tokenFile = (Resolve-Path -LiteralPath $PosthogTokenPath).Path
    $projectToken = [IO.File]::ReadAllText($tokenFile).Trim()
    if ($projectToken -notmatch '^[A-Za-z0-9_-]{16,200}$') { throw 'The file must contain only the project ingestion token.' }
    $secret = 'callayers-v2-posthog'
    $existing = Cloud -Arguments @('secrets','list',"--filter=name:$secret",'--format=value(name)')
    if (-not ($existing | Where-Object { ($_ -split '/')[-1] -eq $secret })) {
        Cloud -Arguments @('secrets','create',$secret,'--replication-policy=automatic') | Out-Null
    }
    Cloud -Arguments @('secrets','versions','add',$secret,"--data-file=$tokenFile") | Out-Null
    Cloud -Arguments @('secrets','add-iam-policy-binding',$secret,"--member=serviceAccount:$identity",'--role=roles/secretmanager.secretAccessor') | Out-Null
    Cloud -Arguments @('run','services','update',$service,"--region=$region","--update-env-vars=POSTHOG_HOST=$PosthogHost","--update-secrets=POSTHOG_PROJECT_TOKEN=${secret}:latest") | Out-Null
    Write-Host 'PostHog host and ingestion token binding configured. Routine deployments preserve these settings.'
    return
}
if ($GenerationOnly) {
    if ($BuildOnly -or $OAuthJsonPath) { throw 'Choose GenerationOnly separately from OAuth/database/build setup.' }
    # Explicit generation provisioning for an existing service. Model, effort
    # and allowance are set independently by the operator, never by this script.
    Cloud -Arguments @('secrets','add-iam-policy-binding','callayers-openai',"--member=serviceAccount:$identity",'--role=roles/secretmanager.secretAccessor') | Out-Null
    Cloud -Arguments @('firestore','fields','ttls','update','expiresAt','--collection-group=requests',"--database=$database",'--enable-ttl') | Out-Null
    Cloud -Arguments @('run','services','update',$service,"--region=$region",'--update-secrets=OPENAI_API_KEY=callayers-openai:latest') | Out-Null
    Write-Host 'Generation permissions, request expiry and provider secret binding configured.'
    return
}

if (-not $BuildOnly) {
    if (-not $OAuthJsonPath) { throw 'Setup requires the downloaded desktop OAuth JSON path.' }
    $resolvedOAuth = (Resolve-Path -LiteralPath $OAuthJsonPath).Path
    $oauthText = [IO.File]::ReadAllText($resolvedOAuth)
    try { $oauth = $oauthText | ConvertFrom-Json } catch { throw 'The supplied OAuth file is not valid JSON.' }
    if ($oauth.installed.project_id -ne $project -or
        $oauth.installed.client_id -ne '955736732096-d4j9im2rjrpa8cda0bvn1trmpqtnafa8.apps.googleusercontent.com' -or
        -not $oauth.installed.client_secret -or $oauth.private_key) {
        throw 'Use the verified Callayers desktop OAuth client JSON, not a web client or service-account key.'
    }
}

if (-not $BuildOnly) {
    Cloud -Arguments @('services','enable','run.googleapis.com','cloudbuild.googleapis.com','artifactregistry.googleapis.com','firestore.googleapis.com','identitytoolkit.googleapis.com','secretmanager.googleapis.com','firebaserules.googleapis.com') | Out-Null
    $databases = Cloud -Arguments @('firestore','databases','list','--format=value(name)')
    if ($databases -notcontains "projects/$project/databases/$database") {
        Cloud -Arguments @('firestore','databases','create',"--database=$database",'--location=nam5','--edition=standard','--type=firestore-native','--delete-protection') | Out-Null
    }
    # Server IAM is scoped to this database. Mobile/web clients have no access.
    $accessToken = (Cloud -Arguments @('auth','print-access-token')).Trim()
    $ruleHeaders = @{ Authorization = "Bearer $accessToken"; 'X-Goog-User-Project' = $project }
    $ruleSource = [IO.File]::ReadAllText((Join-Path $source 'firestore.rules'))
    $ruleset = RulesRequest 'Post' "https://firebaserules.googleapis.com/v1/projects/$project/rulesets" @{ source = @{ files = @(@{ name = 'firestore.rules'; content = $ruleSource }) } } $ruleHeaders
    $releaseName = "projects/$project/releases/cloud.firestore/$database"
    $releaseUri = "https://firebaserules.googleapis.com/v1/$releaseName"
    $exists = $true
    try { Invoke-RestMethod -Method Get -Uri $releaseUri -Headers $ruleHeaders | Out-Null }
    catch { if ([int]$_.Exception.Response.StatusCode -eq 404) { $exists = $false } else { throw 'Could not inspect v2 database access rules.' } }
    $release = @{ name = $releaseName; rulesetName = $ruleset.name }
    if ($exists) {
        RulesRequest 'Patch' $releaseUri @{ release = $release; updateMask = 'rulesetName' } $ruleHeaders | Out-Null
    } else {
        RulesRequest 'Post' "https://firebaserules.googleapis.com/v1/projects/$project/releases" $release $ruleHeaders | Out-Null
    }
    $ruleHeaders = $null; $accessToken = $null

    $identities = Cloud -Arguments @('iam','service-accounts','list',"--filter=email=$identity",'--format=value(email)')
    if ($identities -notcontains $identity) {
        Cloud -Arguments @('iam','service-accounts','create',$service,'--display-name=Callayers v2 API') | Out-Null
        Write-Host 'Waiting one minute for Google to recognize the new service account...'
        Start-Sleep -Seconds 60
    }
    $condition = "expression=resource.name=='projects/$project/databases/$database',title=callayers-v2-only"
    Cloud -Arguments @('projects','add-iam-policy-binding',$project,"--member=serviceAccount:$identity",'--role=roles/datastore.user',"--condition=$condition") | Out-Null
    Cloud -Arguments @('projects','add-iam-policy-binding',$project,"--member=serviceAccount:$identity",'--role=roles/firebaseauth.viewer','--condition=None') | Out-Null

    $secrets = @(Cloud -Arguments @('secrets','list',"--filter=name:$oauthSecret",'--format=value(name)')) |
        ForEach-Object { ($_ -split '/')[-1] }
    if ($secrets -notcontains $oauthSecret) {
        Cloud -Arguments @('secrets','create',$oauthSecret,'--replication-policy=automatic') | Out-Null
    }
    # Pass the file path only. Neither the credential JSON nor its contents go to
    # command arguments, terminal output, Git, or a copied workspace file.
    Cloud -Arguments @('secrets','versions','add',$oauthSecret,"--data-file=$resolvedOAuth") | Out-Null
    Cloud -Arguments @('secrets','add-iam-policy-binding',$oauthSecret,"--member=serviceAccount:$identity",'--role=roles/secretmanager.secretAccessor') | Out-Null
    $oauthText = $null; $oauth = $null
}

$buildIdentities = Cloud -Arguments @('iam','service-accounts','list',"--filter=email=$buildIdentity",'--format=value(email)')
if ($buildIdentities -notcontains $buildIdentity) {
    Cloud -Arguments @('iam','service-accounts','create','callayers-v2-build','--display-name=Callayers v2 source builder') | Out-Null
    Write-Host 'Waiting one minute for Google to recognize the new build account...'
    Start-Sleep -Seconds 60
}
# The old service uses a different artifact repository. Build access covers
# only this source prefix, this source-deploy repository, and log writing.
$sourcePrefix = 'projects/_/buckets/run-sources-callayers-us-central1/objects/services/callayers-v2-api/'
$sourceCondition = "expression=resource.name.startsWith('$sourcePrefix'),title=callayers-v2-source-only"
Cloud -Arguments @('storage','buckets','add-iam-policy-binding','gs://run-sources-callayers-us-central1',
    "--member=serviceAccount:$buildIdentity",'--role=roles/storage.objectViewer',"--condition=$sourceCondition") | Out-Null
Cloud -Arguments @('artifacts','repositories','add-iam-policy-binding','cloud-run-source-deploy',"--location=$region",
    "--member=serviceAccount:$buildIdentity",'--role=roles/artifactregistry.writer') | Out-Null
Cloud -Arguments @('projects','add-iam-policy-binding',$project,"--member=serviceAccount:$buildIdentity",'--role=roles/logging.logWriter','--condition=None') | Out-Null

Write-Host 'V2 identities and database ready. Configure initial Cloud Run settings separately before routine deployment; see backend-v2/README.md.'
