# Run by the user. Samples process resources; does not operate the app or certify
# visual behavior. Uses process CPU time and PrivateMemorySize64, not working set.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('PreviewNoCards', 'PreviewCards', 'CoveredNoConsumer', 'MinimizedMeet', 'MinimizedIdle', 'Quit', 'ForceStop', 'RawCameraMeet')]
    [string]$Scenario,
    [ValidateRange(5, 600)][int]$Seconds = 30,
    [ValidateRange(0, 120)][int]$DelaySeconds = 5,
    [string]$Notes = '',
    [switch]$DiagnosticsBuild
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$appRelativePath = if ($DiagnosticsBuild) { 'build\diagnostics-x64\Release\Callayers.exe' } else { 'build\x64\Release\Callayers.exe' }
$appPath = [IO.Path]::GetFullPath((Join-Path $repo $appRelativePath))
$profilePath = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Callayers\Foundation\WebView2'))
$profilePattern = '(?i)(?:^|\s)"?--user-data-dir=(?:"' + [regex]::Escape($profilePath) + '"|' + [regex]::Escape($profilePath) + ')"?(?:\s|$)'
$shellProcess = [Diagnostics.Process]::GetCurrentProcess()
$sessionNumber = $shellProcess.SessionId
$shellProcess.Dispose()
$logicalProcessors = [Environment]::ProcessorCount
$outputDirectory = Join-Path $repo '.local\verification'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$runName = '{0}-{1}-{2}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $Scenario, ([guid]::NewGuid().ToString('N').Substring(0, 6))
$outputBase = Join-Path $outputDirectory $runName
$tracked = @{}
$warnings = [Collections.Generic.HashSet[string]]::new()
$identityCache = Join-Path $outputDirectory 'renderer-identities.json'
if (Test-Path -LiteralPath $identityCache) {
    try {
        $cache = Get-Content -LiteralPath $identityCache -Raw | ConvertFrom-Json
        if ($cache.AppPath -ieq $appPath) {
            foreach ($key in @($cache.Renderers | Select-Object -Last 256)) { $tracked[[string]$key] = $true }
        }
    } catch { $null = $warnings.Add('Previous renderer identities could not be read. Run a live-preview measurement before measuring quit cleanup.') }
}
if ($Scenario -in @('Quit', 'ForceStop') -and -not $tracked.Count) {
    $null = $warnings.Add('No earlier renderer identities are available. Orphans without a surviving profile process may be unattributed; zero processes alone cannot establish cleanup. First measure live preview, then repeat the shutdown measurement.')
}
$samples = [Collections.Generic.List[object]]::new()
$processRows = [Collections.Generic.List[object]]::new()
$clock = [Diagnostics.Stopwatch]::StartNew()

function Get-ResourceSnapshot {
    $startedAt = $clock.Elapsed.TotalSeconds
    # Restrict discovery to relevant executable names in this Windows session.
    # Command lines are used only in memory to identify our WebView2 profile.
    $inventory = @(Get-CimInstance Win32_Process -Filter "SessionId = $sessionNumber AND (Name = 'Callayers.exe' OR Name = 'msedgewebview2.exe' OR Name = 'brave.exe')" -Property ProcessId, ParentProcessId, CreationDate, ExecutablePath, CommandLine, Name)
    $current = @{}
    foreach ($entry in $inventory) {
        if ($null -eq $entry.CreationDate) {
            $null = $warnings.Add("Process identity unavailable for $($entry.Name), PID $($entry.ProcessId). It was not silently counted as zero.")
            continue
        }
        $born = ([datetime]$entry.CreationDate).ToUniversalTime()
        $processKey = '{0}:{1}' -f $entry.ProcessId, $born.Ticks
        $role = $null
        if ($entry.Name -ieq 'brave.exe') { $role = 'Brave' }
        elseif ($entry.Name -ieq 'Callayers.exe') {
            if ([string]::IsNullOrEmpty($entry.ExecutablePath)) {
                $null = $warnings.Add("Cannot identify the executable path for Callayers PID $($entry.ProcessId); this sample may be incomplete.")
            } elseif ($entry.ExecutablePath -ieq $appPath) { $role = 'Desktop' }
        } elseif ($tracked.ContainsKey($processKey)) { $role = 'Renderer' }
        elseif ($entry.CommandLine -match $profilePattern) { $role = 'Renderer' }
        $current[[int]$entry.ProcessId] = [pscustomobject]@{
            Key = $processKey; ProcessId = [int]$entry.ProcessId; ParentId = [int]$entry.ParentProcessId
            BornUtc = $born; Role = $role; Name = $entry.Name
        }
    }
    # Follow renderer descendants, retaining their birth identities between
    # samples so survivors remain visible after the desktop exits.
    do {
        $added = $false
        foreach ($record in $current.Values) {
            if ($record.Role -or $record.Name -ine 'msedgewebview2.exe') { continue }
            $parent = $current[$record.ParentId]
            if ($null -ne $parent -and $parent.Role -in @('Desktop', 'Renderer') -and $parent.BornUtc -le $record.BornUtc) {
                $record.Role = 'Renderer'; $added = $true
            }
        }
    } while ($added)
    $measurements = @{}
    foreach ($record in $current.Values) {
        if (-not $record.Role) { continue }
        if ($record.Role -eq 'Renderer') { $tracked[$record.Key] = $true }
        $process = $null
        try {
            $process = [Diagnostics.Process]::GetProcessById($record.ProcessId)
            # GetProcessById cannot atomically bind to the earlier CIM snapshot;
            # reject PID reuse before reading counters.
            $processStart = $process.StartTime.ToUniversalTime()
            if ([math]::Abs(($processStart - $record.BornUtc).TotalMilliseconds) -gt 1) {
                $null = $warnings.Add('A process identity changed while sampling. Inspect process churn before comparing CPU averages.')
                continue
            }
            $process.Refresh()
            $measurements[$record.Key] = [pscustomobject]@{
                ProcessId = $record.ProcessId; Role = $record.Role; Name = $record.Name
                BornUtc = $record.BornUtc; CpuSeconds = $process.TotalProcessorTime.TotalSeconds
                PrivateBytes = [long]$process.PrivateMemorySize64
            }
        } catch {
            $null = $warnings.Add("A $($record.Role) process exited or could not be read while sampling (PID $($record.ProcessId)). CPU may be understated; inspect the process rows.")
        } finally { if ($null -ne $process) { $process.Dispose() } }
    }
    [pscustomobject]@{
        AtSeconds = ($startedAt + $clock.Elapsed.TotalSeconds) / 2
        Utc = [datetime]::UtcNow
        Processes = $measurements
        UnattributedWebViews = @($current.Values | Where-Object { $_.Name -ieq 'msedgewebview2.exe' -and -not $_.Role }).Count
    }
}

$instructions = @{
    PreviewNoCards = 'Close Meet camera use. Keep Callayers restored. Uncheck all cards and clear annotations.'
    PreviewCards = 'Close Meet camera use. Keep Callayers restored. Show all three cards and draw one annotation.'
    CoveredNoConsumer = 'Close Meet camera use. Keep all cards/annotation. Cover Callayers with another window; do not minimize it.'
    MinimizedMeet = 'Keep Meet actively receiving Callayers with all cards/annotation. Explicitly minimize Callayers.'
    MinimizedIdle = 'Close the Meet tab so it releases the camera, then explicitly minimize Callayers.'
    Quit = 'Quit Callayers normally before sampling. Keep Brave in the same state you want recorded.'
    ForceStop = 'Use force-stop-foundation.ps1 yourself, then sample for any surviving Callayers renderer processes.'
    RawCameraMeet = 'Quit Callayers. In the same Meet call, choose the same physical webcam directly. Keep participants, video resolution, effects, and other Brave tabs the same as the Callayers comparison.'
}
Write-Host "Scenario: $Scenario"
Write-Host $instructions[$Scenario]
Write-Host "You have $DelaySeconds seconds to arrange the windows; then sampling runs for $Seconds seconds."
Write-Host 'The label is your chosen scenario, not an automatic check of app state.'
if ($DelaySeconds -gt 0) { Start-Sleep -Seconds $DelaySeconds }

# First enumeration must succeed. Permission/query failures abort, never appear
# as zero CPU or a successful exit check.
try { $previous = Get-ResourceSnapshot } catch { throw "Cannot read process metrics: $($_.Exception.Message). No pass/fail result was generated. Run from your regular Windows PowerShell session." }
$began = $previous.AtSeconds
$wallStart = [datetime]::UtcNow
$nextReport = 10
do {
    Start-Sleep -Seconds 1
    $currentSample = Get-ResourceSnapshot
    $interval = $currentSample.AtSeconds - $previous.AtSeconds
    $elapsed = $currentSample.AtSeconds - $began
    foreach ($group in @('Desktop', 'Renderer', 'CallayersTotal', 'Brave')) {
        $roles = if ($group -eq 'CallayersTotal') { @('Desktop', 'Renderer') } else { @($group) }
        [double]$cpuDelta = 0
        [long]$privateBytes = 0
        $count = 0
        foreach ($pair in $currentSample.Processes.GetEnumerator()) {
            $process = $pair.Value
            if ($process.Role -notin $roles) { continue }
            $count++
            $privateBytes += $process.PrivateBytes
            if ($previous.Processes.ContainsKey($pair.Key)) {
                # Explicit doubles prevent Windows PowerShell 5.1 from choosing
                # an integer Math.Max overload for this computed argument and
                # rounding small CPU increments to zero (or larger ones upward).
                $cpuDelta += [math]::Max([double]0, [double]($process.CpuSeconds - $previous.Processes[$pair.Key].CpuSeconds))
            } else {
                # A newly observed process lacks an interval baseline. Exclude
                # its lifetime CPU, flagging churn rather than creating a spike.
                $null = $warnings.Add('Processes appeared during measurement; their first CPU interval is excluded. Repeat steady-state scenarios after startup settles.')
            }
        }
        foreach ($pair in $previous.Processes.GetEnumerator()) {
            if ($pair.Value.Role -in $roles -and -not $currentSample.Processes.ContainsKey($pair.Key)) {
                $null = $warnings.Add('Processes disappeared during measurement; their last CPU interval cannot be recovered. CPU is a lower bound for intervals with process exits.')
            }
        }
        $samples.Add([pscustomobject]@{
            Scenario = $Scenario; Utc = $currentSample.Utc.ToString('o'); ElapsedSeconds = $elapsed
            IntervalSeconds = $interval; Group = $group; ProcessCount = $count
            CpuSeconds = $cpuDelta; CpuPercent = 100 * $cpuDelta / ($interval * $logicalProcessors)
            PrivateMiB = $privateBytes / 1MB; UnattributedWebViews = $currentSample.UnattributedWebViews
        })
    }
    foreach ($process in $currentSample.Processes.Values) {
        $processRows.Add([pscustomobject]@{
            Utc = $currentSample.Utc.ToString('o'); Group = $process.Role; ProcessId = $process.ProcessId
            BornUtc = $process.BornUtc.ToString('o'); Name = $process.Name
            TotalCpuSeconds = $process.CpuSeconds; PrivateMiB = $process.PrivateBytes / 1MB
        })
    }
    if ($elapsed -ge $nextReport) {
        Write-Host ('Sampled {0:N0} / {1} seconds' -f $elapsed, $Seconds)
        $nextReport += 10
    }
    $previous = $currentSample
} while ($elapsed -lt $Seconds)

$summaryRows = foreach ($group in @('Desktop', 'Renderer', 'CallayersTotal', 'Brave')) {
    $rows = @($samples | Where-Object { $_.Group -eq $group })
    $totalTime = ($rows | Measure-Object IntervalSeconds -Sum).Sum
    $totalCpu = ($rows | Measure-Object CpuSeconds -Sum).Sum
    [pscustomobject]@{
        Group = $group
        AverageCpuPercent = [math]::Round(100 * $totalCpu / ($totalTime * $logicalProcessors), 3)
        PeakSampleCpuPercent = [math]::Round(($rows | Measure-Object CpuPercent -Maximum).Maximum, 3)
        AveragePrivateMiB = [math]::Round(($rows | Measure-Object PrivateMiB -Average).Average, 2)
        PeakPrivateMiB = [math]::Round(($rows | Measure-Object PrivateMiB -Maximum).Maximum, 2)
        EndProcessCount = $rows[-1].ProcessCount
    }
}
$revision = $null
if (Get-Command git -ErrorAction SilentlyContinue) { $revision = & git -C $repo rev-parse --short HEAD 2>$null }
$appHash = if (Test-Path -LiteralPath $appPath) { (Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash } else { $null }
$braveVersion = $null
foreach ($candidate in @("$env:ProgramFiles\BraveSoftware\Brave-Browser\Application\brave.exe", "${env:ProgramFiles(x86)}\BraveSoftware\Brave-Browser\Application\brave.exe", "$env:LOCALAPPDATA\BraveSoftware\Brave-Browser\Application\brave.exe")) {
    if (Test-Path -LiteralPath $candidate) { $braveVersion = (Get-Item -LiteralPath $candidate).VersionInfo.ProductVersion; break }
}
$report = [ordered]@{
    MeasurementVersion = 2
    Scenario = $Scenario; Notes = $Notes; StartedUtc = $wallStart.ToString('o'); DurationSeconds = $elapsed
    Computer = $env:COMPUTERNAME; WindowsVersion = [Environment]::OSVersion.VersionString
    PowerShellVersion = $PSVersionTable.PSVersion.ToString(); LogicalProcessors = $logicalProcessors
    CpuMethod = 'Delta process CPU seconds / elapsed seconds / logical processors * 100. 100% means the whole machine; not frequency-adjusted Task Manager utilization.'
    MemoryMethod = 'Process.PrivateMemorySize64, summed by group; MiB = bytes / 1048576. Not working set.'
    Scope = 'This build desktop + attributed WebView2 descendants/profile. All Brave processes in this Windows session, not only Meet. Other WebView2 apps excluded.'
    Revision = $revision; ExecutableSha256 = $appHash; AppPath = $appPath; BraveVersion = $braveVersion
    VerificationStatus = 'Measurement only. User observation required; no automatic pass/fail.'
    Warnings = @($warnings | Sort-Object); Summary = @($summaryRows)
}
$samples | Export-Csv -LiteralPath "$outputBase-samples.csv" -NoTypeInformation -Encoding UTF8
if ($processRows.Count) { $processRows | Export-Csv -LiteralPath "$outputBase-processes.csv" -NoTypeInformation -Encoding UTF8 }
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$outputBase-summary.json" -Encoding UTF8
[ordered]@{ AppPath = $appPath; Renderers = @($tracked.Keys | Select-Object -Last 256) } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $identityCache -Encoding UTF8
$summaryRows | Format-Table -AutoSize | Out-Host
foreach ($warning in $warnings) { Write-Warning $warning }
Write-Host "Saved: $outputBase-summary.json"
Write-Host 'Report your visual observations separately. These numbers do not establish frame delivery, camera release, or a pass.'
