param(
    [string]$RepoRoot = "",
    [string]$Base = "12e8d4a956",
    [string]$Manifest = "TG_PROBES/tg_change_fence_manifest.json",
    [string]$Config = "TG_PROBES/tg_change_rehearsal_config.json",
    [switch]$SkipRehearsal,
    [switch]$NoReportReuse
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\.." )).Path
}

$Python = Join-Path $RepoRoot "..\.venv\Scripts\python.exe"
if (-not (Test-Path -LiteralPath $Python)) {
    throw "Python interpreter not found: $Python"
}

$rehearsalConfig = Get-Content -LiteralPath (Join-Path $RepoRoot $Config) -Raw | ConvertFrom-Json
$packet26Config = @($rehearsalConfig.commands | Where-Object { $_.name -eq "packet26-test" })[0]
$packet30Config = @($rehearsalConfig.commands | Where-Object { $_.name -eq "packet30-test" })[0]
if (-not ($packet26Config.tokens -contains "{build}/Debug/tg.exe")) {
    throw "packet26 rehearsal test must use isolated build binary"
}
if (-not ($packet30Config.tokens -contains "{build}/Debug/tg.exe")) {
    throw "packet30 rehearsal test must use isolated build binary"
}

Push-Location $RepoRoot
$tempArtifacts = @()
$snapshotProbePath = ""

function Test-RehearsalReportReusable {
    param(
        [string]$ReportPath,
        [string]$Mode,
        [datetime]$ManifestWriteUtc,
        [datetime]$ConfigWriteUtc,
        [string]$HeadExpected,
        [string]$InputFingerprintExpected,
        [string]$TargetShaExpected,
        [string]$ManifestRel,
        [string]$ConfigRel
    )

    if (-not (Test-Path -LiteralPath $ReportPath)) {
        return $false
    }
    $reportInfo = Get-Item -LiteralPath $ReportPath
    if ($reportInfo.LastWriteTimeUtc -lt $ManifestWriteUtc -or $reportInfo.LastWriteTimeUtc -lt $ConfigWriteUtc) {
        return $false
    }

    $doc = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
    if ([string]$doc.mode -ne 'rehearsal') { return $false }
    if ([string]$doc.summary.mode -ne $Mode) { return $false }
    if ([string]$doc.summary.manifest -ne ($ManifestRel -replace '\\', '/')) { return $false }
    if ([string]$doc.summary.config -ne ($ConfigRel -replace '\\', '/')) { return $false }
    if ([string]$doc.summary.syntheticCommit -eq '') { return $false }
    if ([string]$doc.summary.inputFingerprint -ne $InputFingerprintExpected) { return $false }
    if ([string]$doc.summary.targetSha -ne $TargetShaExpected) { return $false }
    if (-not $doc.summary.primaryIntegrity.headUnchanged) { return $false }
    if (-not $doc.summary.primaryIntegrity.indexTreeUnchanged) { return $false }
    if (-not $doc.summary.primaryIntegrity.statusUnchanged) { return $false }
    if (-not $doc.summary.primaryIntegrity.localConfigUnchanged) { return $false }

    $integrityPhase = $doc.summary.phases | Where-Object { $_.name -eq 'primary-integrity' } | Select-Object -First 1
    if ($null -eq $integrityPhase) { return $false }
    if ([string]$integrityPhase.status -ne 'pass') { return $false }
    if ([string]$integrityPhase.head -ne $HeadExpected) { return $false }

    if ($Mode -eq 'rebase' -and [string]$doc.summary.semanticMode -eq '') {
        return $false
    }

    return $true
}

try {
    $headBefore = (& git rev-parse HEAD).Trim()
    $inputFingerprint = (& $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py --repo . --print-input-fingerprint).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($inputFingerprint)) {
        throw "failed to compute rehearsal input fingerprint"
    }
    $fingerprintProbePath = Join-Path $RepoRoot "TG_PROBES\packet31_fingerprint_probe.tmp"
    Set-Content -LiteralPath $fingerprintProbePath -Value "fingerprint-probe" -NoNewline -Encoding utf8
    try {
        $changedFingerprint = (& $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py --repo . --print-input-fingerprint).Trim()
        if ($LASTEXITCODE -ne 0 -or $changedFingerprint -eq $inputFingerprint) {
            throw "rehearsal input fingerprint did not change for an untracked behavior input"
        }
    }
    finally {
        Remove-Item -LiteralPath $fingerprintProbePath -Force -ErrorAction SilentlyContinue
    }
    $restoredFingerprint = (& $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py --repo . --print-input-fingerprint).Trim()
    if ($LASTEXITCODE -ne 0 -or $restoredFingerprint -ne $inputFingerprint) {
        throw "rehearsal input fingerprint did not restore after probe removal"
    }
    $targetSha = (& git rev-parse upstream/dev).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($targetSha)) {
        throw "failed to resolve rehearsal target upstream/dev"
    }
    $statusBefore = (& git status --short) -join "`n"

    $reportsDir = Join-Path $RepoRoot "TG_PROBES\reports"
    New-Item -ItemType Directory -Path $reportsDir -Force | Out-Null

    & $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py --self-test
    if ($LASTEXITCODE -ne 0) { throw "self-test failed with exit $LASTEXITCODE" }

    & $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py --repo . --base $Base --json-report TG_PROBES/reports/fence_validate_legacy.json
    if ($LASTEXITCODE -ne 0) { throw "legacy validate failed with exit $LASTEXITCODE" }

    & $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py validate --repo . --base $Base --json-report TG_PROBES/reports/fence_validate.json
    if ($LASTEXITCODE -ne 0) { throw "validate failed with exit $LASTEXITCODE" }

    & $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py quality --repo . --base $Base --manifest $Manifest --warn-fence-lines 40 --max-fence-lines 80 --warn-min-change-density 0.50 --min-change-density 0.20 --warn-stale-days 180 --max-stale-days 365 --json-report TG_PROBES/reports/fence_quality.json
    if ($LASTEXITCODE -ne 0) { throw "quality failed with exit $LASTEXITCODE" }

    & $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py inventory --repo . --base $Base --manifest $Manifest --check --json-report TG_PROBES/reports/fence_inventory_check.json
    if ($LASTEXITCODE -ne 0) { throw "inventory check failed with exit $LASTEXITCODE" }

    $tempManifestInvalidBase = Join-Path $env:TEMP "packet31_manifest_invalid_base_$([guid]::NewGuid().ToString('N')).json"
    $tempManifestZeroCommit = Join-Path $env:TEMP "packet31_manifest_zero_commit_$([guid]::NewGuid().ToString('N')).json"
    $tempConfigUnknownPlaceholder = Join-Path $env:TEMP "packet31_config_unknown_placeholder_$([guid]::NewGuid().ToString('N')).json"
    $tempUnknownPlaceholderReport = Join-Path $env:TEMP "packet31_unknown_placeholder_report_$([guid]::NewGuid().ToString('N')).json"
    $tempArtifacts += $tempManifestInvalidBase
    $tempArtifacts += $tempManifestZeroCommit
    $tempArtifacts += $tempConfigUnknownPlaceholder
    $tempArtifacts += $tempUnknownPlaceholderReport

    $manifestDoc = Get-Content -LiteralPath (Join-Path $RepoRoot $Manifest) -Raw | ConvertFrom-Json
    $invalidBaseDoc = $manifestDoc | ConvertTo-Json -Depth 100 | ConvertFrom-Json
    $invalidBaseDoc.baseRevision = ('0' * 40)
    [System.IO.File]::WriteAllText(
        $tempManifestInvalidBase,
        ($invalidBaseDoc | ConvertTo-Json -Depth 100),
        (New-Object System.Text.UTF8Encoding($false))
    )

    & $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py quality --repo . --base $Base --manifest $tempManifestInvalidBase --warn-fence-lines 40 --max-fence-lines 80 --warn-min-change-density 0.50 --min-change-density 0.20 --warn-stale-days 180 --max-stale-days 365
    if ($LASTEXITCODE -ne 2) { throw "zero SHA baseRevision should be rejected with exit 2, got $LASTEXITCODE" }

    $zeroCommitDoc = $manifestDoc | ConvertTo-Json -Depth 100 | ConvertFrom-Json
    if ($zeroCommitDoc.fences.Count -gt 0) {
        $zeroCommitDoc.fences[0].lastTouchedCommit = ('0' * 40)
    }
    [System.IO.File]::WriteAllText(
        $tempManifestZeroCommit,
        ($zeroCommitDoc | ConvertTo-Json -Depth 100),
        (New-Object System.Text.UTF8Encoding($false))
    )

    & $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py inventory --repo . --base $Base --manifest $tempManifestZeroCommit --check
    if ($LASTEXITCODE -eq 2) { throw "lastTouchedCommit zero SHA normalization should not fail schema/input" }

    $invalidConfigDoc = $rehearsalConfig | ConvertTo-Json -Depth 100 | ConvertFrom-Json
    $invalidConfigDoc.commands[0].tokens[0] = "{unknown-placeholder}"
    [System.IO.File]::WriteAllText(
        $tempConfigUnknownPlaceholder,
        ($invalidConfigDoc | ConvertTo-Json -Depth 100),
        (New-Object System.Text.UTF8Encoding($false))
    )
    & $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py rehearsal --repo . --manifest $Manifest --config $tempConfigUnknownPlaceholder --mode merge --target upstream/dev --no-fetch --json-report $tempUnknownPlaceholderReport
    if ($LASTEXITCODE -ne 2) { throw "unknown command placeholder should fail with exit 2, got $LASTEXITCODE" }
    $unknownPlaceholderReport = Get-Content -LiteralPath $tempUnknownPlaceholderReport -Raw | ConvertFrom-Json
    if ([int]$unknownPlaceholderReport.exitCode -ne 2 -or [string]$unknownPlaceholderReport.summary.error -notmatch 'unknown placeholder') {
        throw "unknown command placeholder report should contain deterministic input error"
    }

    if (-not $SkipRehearsal) {
        $mergeExit = 0
        $rebaseExit = 0
        $fetchParserExit = 0

        $mergeReportPath = Join-Path $RepoRoot "TG_PROBES\reports\rehearsal_local_merge.json"
        $rebaseReportPath = Join-Path $RepoRoot "TG_PROBES\reports\rehearsal_local_rebase.json"
        $manifestPath = Join-Path $RepoRoot $Manifest
        $configPath = Join-Path $RepoRoot $Config
        $manifestWriteUtc = (Get-Item -LiteralPath $manifestPath).LastWriteTimeUtc
        $configWriteUtc = (Get-Item -LiteralPath $configPath).LastWriteTimeUtc
        $reuseMerge = $false
        $reuseRebase = $false

        if (-not $NoReportReuse) {
            $reuseMerge = Test-RehearsalReportReusable -ReportPath $mergeReportPath -Mode 'merge' -ManifestWriteUtc $manifestWriteUtc -ConfigWriteUtc $configWriteUtc -HeadExpected $headBefore -InputFingerprintExpected $inputFingerprint -TargetShaExpected $targetSha -ManifestRel $Manifest -ConfigRel $Config
            $reuseRebase = Test-RehearsalReportReusable -ReportPath $rebaseReportPath -Mode 'rebase' -ManifestWriteUtc $manifestWriteUtc -ConfigWriteUtc $configWriteUtc -HeadExpected $headBefore -InputFingerprintExpected $inputFingerprint -TargetShaExpected $targetSha -ManifestRel $Manifest -ConfigRel $Config
        }

        $disk = Get-PSDrive -Name ((Get-Item -LiteralPath $RepoRoot).PSDrive.Name)
        Write-Output ("PACKET31_FREE_GB_BEFORE_REHEARSAL={0:N2}" -f ($disk.Free / 1GB))

        if (-not $reuseMerge) {
            $snapshotProbeRelative = "TG_PROBES/reports/packet31_snapshot_probe.txt"
            $snapshotProbePath = Join-Path $RepoRoot ($snapshotProbeRelative -replace '/', '\')
            $snapshotProbeValue = "packet31-snapshot-probe-$([guid]::NewGuid().ToString('N'))"
            Set-Content -LiteralPath $snapshotProbePath -Value $snapshotProbeValue -NoNewline -Encoding utf8
            & $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py rehearsal --repo . --manifest $Manifest --config $Config --mode merge --target upstream/dev --no-fetch --json-report TG_PROBES/reports/rehearsal_local_merge.json
            $mergeExit = $LASTEXITCODE
        }
        $mergeReport = Get-Content -LiteralPath $mergeReportPath -Raw | ConvertFrom-Json
        if ($reuseMerge) {
            $mergeExit = [int]$mergeReport.exitCode
            Write-Output "PACKET31_REUSE_REPORT_MERGE=1"
        }
        $syntheticCommit = [string]$mergeReport.summary.syntheticCommit
        if ([string]::IsNullOrWhiteSpace($syntheticCommit)) {
            throw "rehearsal merge report missing syntheticCommit"
        }
        if (-not $reuseMerge) {
            & git cat-file -e "${syntheticCommit}:$snapshotProbeRelative"
            if ($LASTEXITCODE -ne 0) {
                throw "synthetic snapshot commit does not include current-worktree probe file"
            }
            Remove-Item -LiteralPath $snapshotProbePath -Force
            $snapshotProbePath = ""
        }

        $overlayPhase = $mergeReport.summary.phases | Where-Object { $_.name -eq 'manifest-overlay-refresh' } | Select-Object -First 1
        if ($null -eq $overlayPhase) { throw "merge report missing manifest-overlay-refresh phase" }
        if ([int]$overlayPhase.movedFenceCount -lt 1) { throw "expected movedFenceCount >= 1 for shifted upstream fence positions" }

        $mergeQualityPhase = $mergeReport.summary.phases | Where-Object { $_.name -eq 'fence-quality' } | Select-Object -First 1
        $mergeInventoryPhase = $mergeReport.summary.phases | Where-Object { $_.name -eq 'fence-inventory-check' } | Select-Object -First 1
        if ($null -eq $mergeQualityPhase -or $null -eq $mergeInventoryPhase) { throw "merge report missing quality/inventory phases" }
        if ([string]$mergeQualityPhase.status -ne 'pass') { throw "merge quality phase did not pass" }
        if ([string]$mergeInventoryPhase.status -ne 'pass') { throw "merge inventory phase did not pass" }

        if (-not $reuseRebase) {
            & $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py rehearsal --repo . --manifest $Manifest --config $Config --mode rebase --target upstream/dev --no-fetch --json-report TG_PROBES/reports/rehearsal_local_rebase.json
            $rebaseExit = $LASTEXITCODE
        }
        $rebaseReport = Get-Content -LiteralPath $rebaseReportPath -Raw | ConvertFrom-Json
        if ($reuseRebase) {
            $rebaseExit = [int]$rebaseReport.exitCode
            Write-Output "PACKET31_REUSE_REPORT_REBASE=1"
        }
        $rebaseQualityPhase = $rebaseReport.summary.phases | Where-Object { $_.name -eq 'fence-quality' } | Select-Object -First 1
        $rebaseInventoryPhase = $rebaseReport.summary.phases | Where-Object { $_.name -eq 'fence-inventory-check' } | Select-Object -First 1
        if ($null -eq $rebaseQualityPhase -or $null -eq $rebaseInventoryPhase) { throw "rebase report missing quality/inventory phases" }
        if ([string]$rebaseQualityPhase.status -ne 'pass') { throw "rebase quality phase did not pass" }
        if ([string]$rebaseInventoryPhase.status -ne 'pass') { throw "rebase inventory phase did not pass" }

        $nativeRebasePhase = $rebaseReport.summary.phases | Where-Object { $_.name -eq 'git-rebase-native-attempt' } | Select-Object -First 1
        if ($null -ne $nativeRebasePhase -and [string]$nativeRebasePhase.stderr -match '(?i)could not detach head') {
            $fallbackPlanPhase = $rebaseReport.summary.phases | Where-Object { $_.name -eq 'git-rebase-fallback-plan' } | Select-Object -First 1
            if ($null -eq $fallbackPlanPhase) { throw "rebase fallback plan phase missing after detach-head native stderr" }
            if ([string]$fallbackPlanPhase.status -ne 'pass') { throw "rebase fallback plan phase did not pass after detach-head native stderr" }

            $fallbackReplayPhase = $rebaseReport.summary.phases | Where-Object { $_.name -eq 'git-rebase-fallback-replay' } | Select-Object -First 1
            if ($null -eq $fallbackReplayPhase) { throw "rebase fallback replay phase missing after detach-head native stderr" }
            if ([string]$fallbackReplayPhase.status -ne 'pass') { throw "rebase fallback replay phase did not pass after detach-head native stderr" }

            $fallbackAbortPhase = $rebaseReport.summary.phases | Where-Object { $_.name -eq 'git-rebase-fallback-abort-native' } | Select-Object -First 1
            if ($null -eq $fallbackAbortPhase) { throw "rebase fallback native abort phase missing before reset" }

            if ([string]$rebaseReport.summary.semanticMode -ne 'reset-cherry-pick-linear-rebase') {
                throw "rebase summary semanticMode should be reset-cherry-pick-linear-rebase after detach-head native stderr"
            }
        }

        & $Python .\Telegram\tg_cli\tools\check_tg_change_fences.py rehearsal --repo . --manifest $Manifest --config $Config --mode rebase --target upstream/dev --no-fetch --fetch upstream dev --json-report TG_PROBES/reports/rehearsal_fetch_rebase_parser_negative.json
        $fetchParserExit = $LASTEXITCODE

        $tempRefs = (& git for-each-ref --format="%(refname:short)" refs/heads/tg-rehearsal-*) -join "`n"
        if (-not [string]::IsNullOrWhiteSpace($tempRefs)) {
            throw "temporary rehearsal refs were not cleaned: $tempRefs"
        }

        $worktreeList = (& git worktree list --porcelain) -join "`n"
        if ($worktreeList -match "\.tg-rehearsal-") {
            throw "temporary rehearsal worktrees were not cleaned"
        }

        & powershell -ExecutionPolicy Bypass -File .\Telegram\tg_cli\tools\test_hosted_console_checkpoint_packet26_review.ps1
        if ($LASTEXITCODE -ne 0) { throw "packet26 test failed with exit $LASTEXITCODE" }

        & powershell -ExecutionPolicy Bypass -File .\Telegram\tg_cli\tools\test_hosted_console_accounts_packet30.ps1 -DevProfile ..\tg-dev-profile
        if ($LASTEXITCODE -ne 0) { throw "packet30 test failed with exit $LASTEXITCODE" }

        $sensitiveValues = @()
        $cachePath = Join-Path $RepoRoot "out\CMakeCache.txt"
        if (Test-Path -LiteralPath $cachePath) {
            foreach ($line in (Get-Content -LiteralPath $cachePath)) {
                if ($line -match '^TDESKTOP_API_ID:[^=]*=(.+)$') { $sensitiveValues += $Matches[1] }
                if ($line -match '^TDESKTOP_API_HASH:[^=]*=(.+)$') { $sensitiveValues += $Matches[1] }
            }
        }

        $reportFiles = @(
            "fence_validate_legacy.json",
            "fence_validate.json",
            "fence_quality.json",
            "fence_inventory_check.json",
            "rehearsal_local_merge.json",
            "rehearsal_local_rebase.json",
            "rehearsal_fetch_rebase_parser_negative.json"
        )
        foreach ($name in $reportFiles) {
            $reportPath = Join-Path $reportsDir $name
            if (-not (Test-Path -LiteralPath $reportPath)) { throw "expected report missing: $reportPath" }
            $reportText = Get-Content -LiteralPath $reportPath -Raw
            if ($reportText -match "(?i)(?<![a-z])[a-z]:[\\/][^\r\n\t\s]+") {
                throw "report contains absolute Windows path: $name"
            }
            foreach ($value in $sensitiveValues) {
                if (-not [string]::IsNullOrWhiteSpace($value) -and $reportText.Contains($value)) {
                    throw "report contains sensitive cache value in $name"
                }
            }
        }

        if ($mergeExit -ne 0) { throw "rehearsal local merge failed with exit $mergeExit" }
        if ($rebaseExit -ne 0) { throw "rehearsal local rebase failed with exit $rebaseExit" }
        if ($fetchParserExit -ne 2) { throw "rehearsal explicit-fetch parser negative test expected exit 2, got $fetchParserExit" }
    }

    $headAfter = (& git rev-parse HEAD).Trim()
    $statusAfter = (& git status --short) -join "`n"

    if ($headBefore -ne $headAfter) {
        throw "HEAD changed across packet31 integration test"
    }

    if ($statusBefore -ne $statusAfter) {
        throw "Working tree status changed across packet31 integration test"
    }

    Write-Output "PACKET31_TEST=PASS"
}
finally {
    if (-not [string]::IsNullOrWhiteSpace($snapshotProbePath) -and (Test-Path -LiteralPath $snapshotProbePath)) {
        Remove-Item -LiteralPath $snapshotProbePath -Force
    }
    foreach ($tempPath in $tempArtifacts) {
        if (Test-Path -LiteralPath $tempPath) {
            Remove-Item -LiteralPath $tempPath -Force
        }
    }
    Pop-Location
}
