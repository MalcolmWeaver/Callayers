param([Parameter(Mandatory)][string]$Version, [Parameter(Mandatory)][string]$PublicKeyFile)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'release/update-common.ps1')
Assert-ReleaseVersion $Version
$publicKey=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $PublicKeyFile))|ConvertFrom-Json
if(@($publicKey.PSObject.Properties.Name).Count -ne 2 -or $publicKey.PSObject.Properties.Name -cnotcontains 'Modulus' -or $publicKey.PSObject.Properties.Name -cnotcontains 'Exponent'){throw 'Packaging requires the public key only; private signing material must not be embedded.'}
$rsa=Get-UpdateRsa $publicKey;$rsa.Dispose()
$payload=Join-Path $repo "build/release-$Version-x64/Release"
foreach($required in @('Callayers.exe','CallayersCamera.dll','CallayersLauncher.exe','service-url.txt','Ultralight.dll')){
    if(-not(Test-Path -LiteralPath (Join-Path $payload $required))){throw "Release payload missing $required. Build with -ReleaseBuild first."}
}
# App-local Visual C++ redistributable avoids an elevated system-wide install.
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json|ConvertFrom-Json|Select-Object -First 1
$redist=Get-ChildItem (Join-Path $vs.installationPath 'VC/Redist/MSVC') -Directory|Where-Object {$_.Name -match '^\d+\.'}|Sort-Object {[version]$_.Name} -Descending|Select-Object -First 1
$crt=Get-ChildItem (Join-Path $redist.FullName 'x64') -Directory -Filter '*.CRT'|Select-Object -First 1
Copy-Item -Path (Join-Path $crt.FullName '*.dll') -Destination $payload -Force
$deps=Join-Path $repo '.deps/release-tools';[IO.Directory]::CreateDirectory($deps)|Out-Null
$compiler=Join-Path $deps 'inno-6.4.3/ISCC.exe'
if(-not(Test-Path -LiteralPath $compiler)){
    $download=Join-Path $deps 'innosetup-6.4.3.exe'
    Invoke-WebRequest 'https://github.com/jrsoftware/issrc/releases/download/is-6_4_3/innosetup-6.4.3.exe' -OutFile $download
    if((Get-FileHash $download).Hash -ine 'f3c42116542c4cc57263c5ba6c4feabfc49fe771f2f98a79d2f7628b8762723b'){throw 'Inno Setup checksum mismatch.'}
    $tool=Start-Process -FilePath $download -ArgumentList @('/PORTABLE=1','/CURRENTUSER','/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/DIR="'+(Join-Path $deps 'inno-6.4.3')+'"')) -WindowStyle Hidden -Wait -PassThru
    if($tool.ExitCode -ne 0){throw 'Could not prepare installer compiler.'}
}
$runtime=Join-Path $deps 'WindowsAppRuntimeInstall-1.6.9-x64.exe'
if(-not(Test-Path -LiteralPath $runtime)){Invoke-WebRequest 'https://aka.ms/windowsappsdk/1.6/1.6.250602001/windowsappruntimeinstall-x64.exe' -OutFile $runtime}
$signature=Get-AuthenticodeSignature -LiteralPath $runtime
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=Microsoft Corporation,'){throw 'Windows runtime signature verification failed.'}
$output=Join-Path $repo "build/installers/$Version";[IO.Directory]::CreateDirectory($output)|Out-Null
Copy-Item -LiteralPath $PublicKeyFile -Destination (Join-Path $output 'public-key.json') -Force
& $compiler "/DReleaseVersion=$Version" "/DPayloadDir=$payload" "/DRuntimeInstaller=$runtime" "/DPublicKey=$((Resolve-Path -LiteralPath $PublicKeyFile).Path)" "/DOutputDir=$output" (Join-Path $repo 'release/installer.iss')
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed.'}
Write-Host "Installer ready: $output/CallayersSetup-$Version.exe"
