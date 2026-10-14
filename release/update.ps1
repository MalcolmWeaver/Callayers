param([ValidateSet('Stage','Apply','ApplyAndLaunch')][string]$Mode = 'Stage', [int]$WaitForPid = 0)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'update-common.ps1')
$root = $PSScriptRoot
$updates = Join-Path $root 'updates'
$lock = $null; $ownsLock = $false; $launchAllowed = $true

function Write-UpdateStatus([string]$State, [string]$Version = '') {
    try {
        $statusFile = Join-Path $root 'update-status.json'
        $temporary = $statusFile + '.' + $PID + '.tmp'
        [IO.File]::WriteAllText($temporary, (@{state=$State;version=$Version;at=[DateTimeOffset]::UtcNow.ToString('o')} | ConvertTo-Json -Compress))
        Move-Item -LiteralPath $temporary -Destination $statusFile -Force
    } catch { }
}

function Test-AppIdle {
    $appMutex = $null; $acquired = $false
    try {
        $appMutex = [Threading.Mutex]::OpenExisting('Local\Callayers.Foundation.v2.Desktop')
        try { $acquired = $appMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $acquired = $true }
        return $acquired
    } catch [Threading.WaitHandleCannotBeOpenedException] { return $true }
    finally { if ($acquired) { $appMutex.ReleaseMutex() }; if ($appMutex) { $appMutex.Dispose() } }
}

try {
    if ($WaitForPid -gt 0) { Wait-Process -Id $WaitForPid -Timeout 30 -ErrorAction SilentlyContinue }
    $lockName = if($Mode -eq 'Stage'){'Local\Callayers.Updater.Stage.tester'}else{'Local\Callayers.Updater.Apply.tester'}
    $lock = New-Object Threading.Mutex($false, $lockName)
    $timeout = if($Mode -eq 'ApplyAndLaunch'){30000}else{0}
    try { $ownsLock = $lock.WaitOne($timeout) } catch [Threading.AbandonedMutexException] { $ownsLock = $true }
    if(-not $ownsLock -and $Mode -eq 'ApplyAndLaunch'){
        $launchAllowed = $false
        Add-Type -AssemblyName System.Windows.Forms
        [Windows.Forms.MessageBox]::Show('Callayers is finishing an update. Open it again in a moment.', 'Callayers') | Out-Null
    }
    if ($ownsLock) {
        $current = Get-CurrentRelease $root
        $key = [IO.File]::ReadAllText((Join-Path $root 'update-public-key.json')) | ConvertFrom-Json
        $pending = Join-Path $updates 'pending.json'
        if ($Mode -eq 'Stage') {
            Write-UpdateStatus 'checking'
            [IO.Directory]::CreateDirectory($updates) | Out-Null
            $feed = Join-Path $updates 'feed.part'
            Receive-UpdateFile ($script:ReleaseDownloadBase + '/tester/latest.json') $feed 65536
            $envelope = [IO.File]::ReadAllText($feed)
            $release = Get-VerifiedRelease $envelope $key
            if ([version]$release.version -gt [version]$current) {
                $directory = Join-Path $updates $release.version
                [IO.Directory]::CreateDirectory($directory) | Out-Null
                $installer = Join-Path $directory 'setup.exe'
                if (-not (Test-ReleaseFile $installer $release)) {
                    Write-UpdateStatus 'downloading' $release.version
                    $partial = $installer + '.part'
                    Receive-UpdateFile $release.url $partial $release.size
                    if (-not (Test-ReleaseFile $partial $release)) { throw 'Downloaded update rejected.' }
                    Move-Item -LiteralPath $partial -Destination $installer -Force
                }
                [IO.File]::WriteAllText(($pending + '.tmp'), $envelope)
                Move-Item -LiteralPath ($pending + '.tmp') -Destination $pending -Force
                Write-UpdateStatus 'ready' $release.version
            } else { Write-UpdateStatus 'current' $current }
        } elseif ([IO.File]::Exists($pending)) {
            # Reauthenticate manifest and bytes immediately before execution.
            $release = Get-VerifiedRelease ([IO.File]::ReadAllText($pending)) $key
            $installer = Join-Path (Join-Path $updates $release.version) 'setup.exe'
            if ([version]$release.version -le [version]$current) { Remove-Item -LiteralPath $pending -Force }
            elseif (-not (Test-ReleaseFile $installer $release)) { throw 'Staged update rejected.' }
            elseif (-not (Test-AppIdle) -or -not (Test-CameraFileIdle (Join-Path $root ('versions/' + $current + '/CallayersCamera.dll')))) {
                Write-UpdateStatus 'waiting-for-camera' $release.version
            } else {
                Write-UpdateStatus 'installing' $release.version
                $process = Start-Process -FilePath $installer -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/NOCLOSEAPPLICATIONS','/NORESTARTAPPLICATIONS') -WindowStyle Hidden -Wait -PassThru
                if ($process.ExitCode -ne 0 -or (Get-CurrentRelease $root) -cne $release.version) { throw 'Update installation deferred or failed.' }
                if([IO.File]::Exists($pending)){
                    $stillPending=Get-VerifiedRelease ([IO.File]::ReadAllText($pending)) $key
                    if($stillPending.version -ceq $release.version){Remove-Item -LiteralPath $pending -Force}
                }
                # Remove only the authenticated, consumed installer. Retain app versions for recovery.
                Remove-Item -LiteralPath $installer -Force
                Write-UpdateStatus 'installed' $release.version
            }
        }
    }
} catch { Write-UpdateStatus 'deferred' }
finally {
    if ($ownsLock) { $lock.ReleaseMutex() }; if ($lock) { $lock.Dispose() }
    if ($Mode -eq 'ApplyAndLaunch' -and $launchAllowed) {
        try {
            $current = Get-CurrentRelease $root
            $directory = Join-Path $root ('versions/' + $current)
            Start-Process -FilePath (Join-Path $directory 'Callayers.exe') -WorkingDirectory $directory
        } catch {
            Add-Type -AssemblyName System.Windows.Forms
            [Windows.Forms.MessageBox]::Show('Callayers could not start. Run the installer again; your saved cards will be kept.', 'Callayers') | Out-Null
        }
    }
}
