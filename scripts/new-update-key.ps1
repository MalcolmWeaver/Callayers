param([string]$Directory = (Join-Path (Split-Path $PSScriptRoot -Parent) '.local/release-keys'))
$ErrorActionPreference = 'Stop'
[IO.Directory]::CreateDirectory($Directory) | Out-Null
$privatePath = Join-Path $Directory 'private.json'
$publicPath = Join-Path $Directory 'public.json'
if (Test-Path -LiteralPath $privatePath) { throw 'A signing key already exists here. Keep it; do not rotate the key between releases.' }
$rsa = New-Object Security.Cryptography.RSACryptoServiceProvider(3072); $rsa.PersistKeyInCsp = $false
try {
    $parameters = $rsa.ExportParameters($true); $private = @{}; $public = @{}
    foreach ($name in @('Modulus','Exponent','D','P','Q','DP','DQ','InverseQ')) { $private[$name] = [Convert]::ToBase64String($parameters.$name) }
    foreach ($name in @('Modulus','Exponent')) { $public[$name] = $private[$name] }
    [IO.File]::WriteAllText($privatePath, ($private | ConvertTo-Json -Compress))
    [IO.File]::WriteAllText($publicPath, ($public | ConvertTo-Json -Compress))
    if ($env:OS -eq 'Windows_NT') {
        $acl = New-Object Security.AccessControl.FileSecurity
        $acl.SetAccessRuleProtection($true, $false)
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
        $acl.SetOwner($identity)
        $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($identity, 'FullControl', 'Allow')))
        Set-Acl -LiteralPath $privatePath -AclObject $acl
    }
    Write-Host "Public key: $publicPath"
    Write-Host 'Private key created in the ignored local folder. Back it up securely and add its exact JSON to the GitHub UPDATE_SIGNING_KEY secret. Never commit it.'
} finally { $rsa.Dispose() }
