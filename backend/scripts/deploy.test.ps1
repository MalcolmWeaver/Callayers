# Runs deployment orchestration against mocks only; never contacts Google/Firebase.
$ErrorActionPreference = 'Stop'
$deployScript = Join-Path $PSScriptRoot '../deploy.ps1'
$testConfig = [System.IO.Path]::GetTempFileName()
$originalLocation = (Get-Location).Path
function Assert($condition, $message) {
    if (-not $condition) { throw $message }
}
function gcloud {
    $global:deployTestState.calls.Add(($args -join ' '))
    $global:LASTEXITCODE = 0
    if ($args[0] -eq 'run' -and $args[1] -eq 'services') {
        if ($global:deployTestState.failAt -eq 'lookup') { $global:LASTEXITCODE = 1; return }
        return $global:deployTestState.existingOrigin
    }
    if ($args[0] -eq 'projects') { return '123456789' }
    if ($args[0] -eq 'builds' -and $global:deployTestState.failAt -eq 'build') {
        $global:LASTEXITCODE = 1
    }
    if ($args[0] -eq 'run' -and $args[1] -eq 'deploy') {
        $index = [array]::IndexOf($args, '--env-vars-file')
        $global:deployTestState.generatedConfig = $args[$index + 1]
        $global:deployTestState.deployedConfig = Get-Content -LiteralPath $global:deployTestState.generatedConfig -Raw
        if ($global:deployTestState.failAt -eq 'deploy') { $global:LASTEXITCODE = 1 }
    }
}
function npx.cmd {
    $global:deployTestState.calls.Add(('firebase ' + ($args -join ' ')))
    $global:LASTEXITCODE = 0
    if ($global:deployTestState.failAt -eq 'firestore') { $global:LASTEXITCODE = 1 }
}
$global:deployTestState = @{} 
try {
    foreach ($scenario in @('pinned', 'existing', 'first', 'firestore', 'lookup-failure', 'build-failure', 'deploy-failure', 'firestore-failure')) {
        $global:deployTestState.calls = [System.Collections.Generic.List[string]]::new()
        $global:deployTestState.existingOrigin = 'https://existing-service.run.app'
        $global:deployTestState.failAt = ''
        $global:deployTestState.generatedConfig = $null
        $global:deployTestState.deployedConfig = ''
        $config = "GOOGLE_CLOUD_PROJECT: `"callayers`"`nFIREBASE_WEB_API_KEY: `"AIza$('a' * 35)`"`n"
        $expectedOrigin = $global:deployTestState.existingOrigin
        if ($scenario -eq 'pinned') {
            $config += "PUBLIC_ORIGIN: `"https://pinned.example.com/`"`n"
            $expectedOrigin = 'https://pinned.example.com'
        }
        if ($scenario -eq 'first') {
            $global:deployTestState.existingOrigin = ''
            $expectedOrigin = 'https://callayers-api-123456789.us-central1.run.app'
        }
        if ($scenario.EndsWith('-failure')) { $global:deployTestState.failAt = $scenario.Replace('-failure', '') }
        [System.IO.File]::WriteAllText($testConfig, $config)
        $parameters = @{ ProjectId = 'callayers'; EnvironmentFile = $testConfig }
        if ($scenario.StartsWith('firestore')) { $parameters.DeployFirestore = $true }
        $failed = $false
        try { & $deployScript @parameters } catch {
            $failed = $true
            if (-not $scenario.EndsWith('-failure')) { throw }
        }
        Assert ($failed -eq $scenario.EndsWith('-failure')) "$scenario returned the wrong success/failure result"
        Assert ((Get-Content -LiteralPath $testConfig -Raw) -ceq $config) "$scenario changed the user's config"
        Assert ((Get-Location).Path -eq $originalLocation) "$scenario did not restore the working directory"
        Assert (-not ($global:deployTestState.calls | Where-Object { $_ -like 'run services update*' })) "$scenario performed a second rollout"
        if ($global:deployTestState.generatedConfig) {
            Assert (-not (Test-Path -LiteralPath $global:deployTestState.generatedConfig)) "$scenario leaked the temporary config"
            Assert ($global:deployTestState.deployedConfig.Contains("PUBLIC_ORIGIN: `"$expectedOrigin`"")) "$scenario deployed the wrong origin"
            Assert (([regex]::Matches($global:deployTestState.deployedConfig, '(?m)^PUBLIC_ORIGIN:')).Count -eq 1) "$scenario duplicated the origin"
        }
        if (-not $failed) {
            Assert (@($global:deployTestState.calls | Where-Object { $_ -like 'run deploy*' }).Count -eq 1) "$scenario did not deploy exactly once"
            Assert (@($global:deployTestState.calls | Where-Object { $_ -like 'firebase*' }).Count -eq [int]$scenario.StartsWith('firestore')) "$scenario ignored Firestore opt-in"
        }
        if ($scenario -eq 'pinned') { Assert ($global:deployTestState.calls[0] -like 'builds submit*') 'Pinned origin performed unnecessary lookups' }
        if ($scenario -in @('lookup-failure', 'build-failure')) {
            Assert (-not ($global:deployTestState.calls | Where-Object { $_ -like 'run deploy*' })) "$scenario continued to deployment"
        }
        if ($scenario -eq 'deploy-failure') {
            Assert (-not ($global:deployTestState.calls | Where-Object { $_ -like 'firebase*' })) 'Failed rollout continued to Firestore'
        }
        Write-Host "PASS: $scenario"
    }
} finally {
    Remove-Item -LiteralPath $testConfig -Force
    Remove-Variable deployTestState -Scope Global
}
