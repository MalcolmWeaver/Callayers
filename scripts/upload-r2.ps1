param([Parameter(Mandatory)][string]$Object,[Parameter(Mandatory)][string]$File,
      [Parameter(Mandatory)][string]$ContentType,[Parameter(Mandatory)][string]$CacheControl)
$ErrorActionPreference='Stop'
if($Object -notmatch '^callayers-downloads/(tester/latest\.json|releases/[0-9]+\.[0-9]+\.[0-9]+/CallayersSetup-[0-9]+\.[0-9]+\.[0-9]+\.exe)$'){throw 'Unexpected release object.'}
& npx --yes wrangler@4.77.0 r2 object put $Object --file $File --content-type $ContentType --cache-control $CacheControl --remote
if($LASTEXITCODE -ne 0){throw 'R2 upload failed.'}
