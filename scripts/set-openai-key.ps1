param(
    [ValidatePattern('^[a-zA-Z0-9._-]{1,100}$')][string]$Model = 'gpt-6-luna',
    [switch]$Launch
)
$ErrorActionPreference = 'Stop'

if (Get-Process -Name Callayers, CallayersAI -ErrorAction SilentlyContinue) {
    throw 'Save your work and close Callayers first, then run this command again.'
}

# Prompt input is masked and never becomes a command-line argument or history entry.
# Windows user environment variables are persistent, but are not encrypted vault storage.
$secureKey = Read-Host 'OpenAI API key' -AsSecureString
$keyPointer = [IntPtr]::Zero
try {
    $keyPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureKey)
    $plainKey = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($keyPointer)
    if ([string]::IsNullOrWhiteSpace($plainKey) -or $plainKey.Length -gt 2000 -or $plainKey -match '\s') {
        throw 'Enter a nonempty API key without spaces or line breaks.'
    }
    [Environment]::SetEnvironmentVariable('OPENAI_API_KEY', $plainKey, 'User')
    $env:OPENAI_API_KEY = $plainKey
} finally {
    if ($keyPointer -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($keyPointer)
    }
    $plainKey = $null
    $secureKey.Dispose()
}

$settingsDirectory = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Callayers'
[IO.Directory]::CreateDirectory($settingsDirectory) | Out-Null
$settingsPath = Join-Path $settingsDirectory 'ai.json'
$settings = if (Test-Path -LiteralPath $settingsPath) {
    Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
} else { [pscustomobject]@{} }
$settings | Add-Member -NotePropertyName model -NotePropertyValue $Model -Force
$temporaryPath = "$settingsPath.tmp"
[IO.File]::WriteAllText($temporaryPath, ($settings | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporaryPath -Destination $settingsPath -Force
Write-Host "Saved OPENAI_API_KEY for your Windows user and selected $Model."
Write-Host 'A key previously saved in Callayers AI settings still takes precedence.'

if ($Launch) {
    $executable = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release\Callayers.exe'
    Start-Process -FilePath $executable -ArgumentList '--webcam', '--publish'
}
