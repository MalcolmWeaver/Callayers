$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'release/update-common.ps1')
$rsa=New-Object Security.Cryptography.RSACryptoServiceProvider(3072);$rsa.PersistKeyInCsp=$false
$parameters=$rsa.ExportParameters($false)
$key=[pscustomobject]@{Modulus=[Convert]::ToBase64String($parameters.Modulus);Exponent=[Convert]::ToBase64String($parameters.Exponent)}
$directory=Join-Path ([IO.Path]::GetTempPath()) ('callayers-update-test-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($directory)|Out-Null
$count=0
function Assert([bool]$Condition,[string]$Name){if(-not $Condition){throw "Failed: $Name"};$script:count++}
function Reject([scriptblock]$Action,[string]$Name){$rejected=$false;try{&$Action|Out-Null}catch{$rejected=$true};Assert $rejected $Name}
function Envelope($Payload){
 $bytes=[Text.Encoding]::UTF8.GetBytes(($Payload|ConvertTo-Json -Compress))
 $signature=$rsa.SignData($bytes,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
 return (@{payload=[Convert]::ToBase64String($bytes);signature=[Convert]::ToBase64String($signature)}|ConvertTo-Json -Compress)
}
try{
 $file=Join-Path $directory 'setup.exe';[IO.File]::WriteAllText($file,'test installer bytes; never executed')
 $payload=[ordered]@{schema=1;channel='tester';version='0.3.1';url='https://downloads.callayers.com/releases/0.3.1/CallayersSetup-0.3.1.exe';sha256=(Get-FileHash $file).Hash.ToLowerInvariant();size=(Get-Item $file).Length;publishedAt=[DateTimeOffset]::UtcNow.ToString('o')}
 $signed=Envelope $payload;$release=Get-VerifiedRelease $signed $key
 Assert (Test-ReleaseFile $file $release) 'authentic matching installer'
 Assert ([version]$release.version -gt [version]'0.3.0') 'numeric newer-version ordering'
 Assert ([version]'0.10.0' -gt [version]'0.9.9') 'double-digit version ordering'
 $tampered=$signed|ConvertFrom-Json;$tampered.payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes('{}'))
 Reject {Get-VerifiedRelease ($tampered|ConvertTo-Json -Compress) $key} 'tampered payload rejected'
 $other=New-Object Security.Cryptography.RSACryptoServiceProvider(3072);$other.PersistKeyInCsp=$false;$op=$other.ExportParameters($false)
 $wrongKey=[pscustomobject]@{Modulus=[Convert]::ToBase64String($op.Modulus);Exponent=[Convert]::ToBase64String($op.Exponent)};$other.Dispose()
 Reject {Get-VerifiedRelease $signed $wrongKey} 'wrong signing identity rejected'
 [IO.File]::AppendAllText($file,'tampered');Assert (-not(Test-ReleaseFile $file $release)) 'modified installer rejected'
 $payload.url='https://evil.example/setup.exe';Reject {Get-VerifiedRelease (Envelope $payload) $key} 'signed unexpected host rejected'
 $payload.url='https://downloads.callayers.com/releases/0.3.1/CallayersSetup-0.3.1.exe';$payload.version='../../outside'
 Reject {Get-VerifiedRelease (Envelope $payload) $key} 'signed traversal version rejected'
 $payload.version='0.3.1';$payload.size=536870913
 Reject {Get-VerifiedRelease (Envelope $payload) $key} 'oversized signed installer rejected'
 Reject {Assert-ReleaseVersion '0.3.0;anything'} 'workflow input injection rejected'
 Reject {Assert-ReleaseVersion '65536.0.0'} 'version overflow rejected'
 $camera=Join-Path $directory 'camera.dll';[IO.File]::WriteAllText($camera,'fixture')
 Assert (Test-CameraFileIdle $camera) 'idle camera file available'
 $locked=[IO.File]::Open($camera,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
 try{Assert (-not(Test-CameraFileIdle $camera)) 'calling-app lock defers update'}finally{$locked.Dispose()}
 Reject {Receive-UpdateFile 'http://downloads.callayers.com/setup.exe' (Join-Path $directory 'bad') 10} 'unencrypted download rejected'
 Reject {Receive-UpdateFile 'https://evil.example/setup.exe' (Join-Path $directory 'bad') 10} 'untrusted host rejected before request'
 Write-Host "$count release/update checks passed."
}finally{
 $rsa.Dispose()
 # Only remove this exact, freshly created fixture directory under the temp root.
 $resolved=[IO.Path]::GetFullPath($directory)
 if($resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase) -and (Split-Path $resolved -Leaf) -like 'callayers-update-test-*'){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
