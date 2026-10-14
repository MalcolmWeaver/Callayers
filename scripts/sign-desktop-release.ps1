param([Parameter(Mandatory)][string]$Installer, [Parameter(Mandatory)][string]$Version,
      [Parameter(Mandatory)][string]$PrivateKeyFile, [string]$Output)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'release/update-common.ps1')
Assert-ReleaseVersion $Version
if (-not $Output) { $Output = Join-Path (Split-Path $Installer -Parent) 'latest.json' }
$file = Get-Item -LiteralPath $Installer
$payload = [ordered]@{schema=1;channel='tester';version=$Version;
    url="https://downloads.callayers.com/releases/$Version/CallayersSetup-$Version.exe";
    sha256=(Get-FileHash -LiteralPath $Installer -Algorithm SHA256).Hash.ToLowerInvariant();
    size=$file.Length;publishedAt=[DateTimeOffset]::UtcNow.ToString('o')}
$bytes = [Text.Encoding]::UTF8.GetBytes(($payload | ConvertTo-Json -Compress))
$key = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $PrivateKeyFile)) | ConvertFrom-Json
$rsa = Get-UpdateRsa $key
try {
    $signature = $rsa.SignData($bytes, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $envelope = @{payload=[Convert]::ToBase64String($bytes);signature=[Convert]::ToBase64String($signature)} | ConvertTo-Json -Compress
    Get-VerifiedRelease $envelope $key | Out-Null
    [IO.File]::WriteAllText($Output, $envelope, [Text.UTF8Encoding]::new($false))
} finally { $rsa.Dispose() }
