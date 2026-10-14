Set-StrictMode -Version Latest

function Get-UpdateRsa($Key) {
    $parameters = New-Object System.Security.Cryptography.RSAParameters
    foreach ($name in @('Modulus','Exponent','D','P','Q','DP','DQ','InverseQ')) {
        if ($Key.PSObject.Properties.Name -contains $name) {
            $parameters.$name = [Convert]::FromBase64String([string]$Key.$name)
        }
    }
    $rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider(3072)
    $rsa.PersistKeyInCsp = $false
    $rsa.ImportParameters($parameters)
    if ($rsa.KeySize -lt 3072) { $rsa.Dispose(); throw 'Update signing key must be at least 3072 bits.' }
    return $rsa
}

function Assert-ReleaseVersion([string]$Version) {
    if ($Version -cnotmatch '^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$') { throw 'Use a numeric release version such as 0.3.0.' }
    foreach ($part in $Version.Split('.')) { if ([int]$part -gt 65535) { throw 'Release version component too large.' } }
}

function Get-VerifiedRelease([string]$Envelope, $PublicKey) {
    if ($Envelope.Length -gt 65536) { throw 'Update manifest too large.' }
    $record = $Envelope | ConvertFrom-Json
    if (@($record.PSObject.Properties.Name).Count -ne 2 -or
        $record.PSObject.Properties.Name -cnotcontains 'payload' -or
        $record.PSObject.Properties.Name -cnotcontains 'signature') { throw 'Invalid update envelope.' }
    $bytes = [Convert]::FromBase64String([string]$record.payload)
    $signature = [Convert]::FromBase64String([string]$record.signature)
    $rsa = Get-UpdateRsa $PublicKey
    try {
        if (-not $rsa.VerifyData($bytes, $signature, [Security.Cryptography.HashAlgorithmName]::SHA256,
                [Security.Cryptography.RSASignaturePadding]::Pkcs1)) { throw 'Update signature rejected.' }
    } finally { $rsa.Dispose() }
    $item = [Text.Encoding]::UTF8.GetString($bytes) | ConvertFrom-Json
    $names = @('schema','channel','version','url','sha256','size','publishedAt')
    if (@($item.PSObject.Properties.Name).Count -ne $names.Count) { throw 'Invalid update fields.' }
    foreach ($name in $names) { if ($item.PSObject.Properties.Name -cnotcontains $name) { throw 'Invalid update fields.' } }
    if ($item.schema -ne 1 -or $item.channel -cne 'tester') { throw 'Unsupported update channel.' }
    Assert-ReleaseVersion ([string]$item.version)
    $expected = 'https://downloads.callayers.com/releases/' + $item.version + '/CallayersSetup-' + $item.version + '.exe'
    if ($item.url -cne $expected) { throw 'Unexpected update download address.' }
    if ([string]$item.sha256 -cnotmatch '^[a-f0-9]{64}$' -or $item.size -lt 1 -or $item.size -gt 536870912 -or
        [double]$item.size -ne [math]::Truncate([double]$item.size)) { throw 'Invalid update file metadata.' }
    $published = [DateTimeOffset]::Parse([string]$item.publishedAt, [Globalization.CultureInfo]::InvariantCulture)
    if ($published -gt [DateTimeOffset]::UtcNow.AddMinutes(10)) { throw 'Update timestamp is in the future.' }
    return $item
}

function Test-ReleaseFile([string]$Path, $Release) {
    if (-not [IO.File]::Exists($Path) -or (Get-Item -LiteralPath $Path).Length -ne $Release.size) { return $false }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $Release.sha256
}

function Test-CameraFileIdle([string]$Path) {
    if (-not [IO.File]::Exists($Path)) { return $false }
    try {
        # Opening writable and exclusive detects image mappings without modifying bytes.
        $handle = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $handle.Dispose()
        return $true
    } catch { return $false }
}

function Get-CurrentRelease([string]$Root) {
    $version = [IO.File]::ReadAllText((Join-Path $Root 'current-version.txt')).Trim()
    Assert-ReleaseVersion $version
    return $version
}

function Receive-UpdateFile([string]$Url, [string]$Path, [long]$Limit) {
    $uri = [Uri]$Url
    if ($uri.Scheme -cne 'https' -or $uri.Host -cne 'downloads.callayers.com' -or $uri.Port -ne 443 -or $uri.UserInfo) { throw 'Untrusted download host.' }
    Add-Type -AssemblyName System.Net.Http
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $handler = New-Object Net.Http.HttpClientHandler
    $handler.AllowAutoRedirect = $false
    $client = New-Object Net.Http.HttpClient($handler)
    $client.Timeout = [TimeSpan]::FromSeconds(90)
    $response = $null; $source = $null; $destination = $null
    try {
        $response = $client.GetAsync($uri, [Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
        $response.EnsureSuccessStatusCode() | Out-Null
        if ($response.Content.Headers.ContentLength -gt $Limit) { throw 'Update download too large.' }
        $source = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $destination = [IO.File]::Create($Path)
        $buffer = New-Object byte[] 65536
        $total = 0L
        $deadline = [DateTime]::UtcNow.AddMinutes(5)
        while ($true) {
            $read = $source.ReadAsync($buffer, 0, $buffer.Length)
            if (-not $read.Wait(30000)) { throw 'Update download timed out.' }
            $count = $read.Result
            if ($count -eq 0) { break }
            $total += $count
            if ($total -gt $Limit -or [DateTime]::UtcNow -gt $deadline) { throw 'Update download limit exceeded.' }
            $destination.Write($buffer, 0, $count)
        }
    } finally {
        if ($destination) { $destination.Dispose() }; if ($source) { $source.Dispose() }
        if ($response) { $response.Dispose() }; $client.Dispose(); $handler.Dispose()
    }
}
