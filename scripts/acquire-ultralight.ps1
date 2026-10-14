$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$sdk=Join-Path $repo '.deps/ultralight'
$version='2.0.0-beta.2'
$hash='e17b3ca2f3c2063f1d14971e7ef756f28e71f2d426a91d2e21c5236d2dc67e6d'
if(-not (Test-Path -LiteralPath "$sdk/include/Ultralight/Ultralight.h")){
    [IO.Directory]::CreateDirectory($sdk)|Out-Null
    $info=Invoke-RestMethod "https://ultralig.ht/api/v1/sdk/download?platform=windows-x64&version=$version&format=json"
    Invoke-WebRequest $info.url -OutFile "$sdk/sdk.7z"
    if((Get-FileHash "$sdk/sdk.7z").Hash -ine $hash){throw 'Ultralight SDK checksum mismatch.'}
    & tar -xf "$sdk/sdk.7z" -C $sdk
    if($LASTEXITCODE -ne 0){throw 'SDK extraction failed.'}
}
if((Get-Content "$sdk/VERSION.txt" -Raw).Trim() -ne $version){throw "Expected Ultralight $version."}
