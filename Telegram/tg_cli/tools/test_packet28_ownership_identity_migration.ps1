param(
    [Parameter(Mandatory = $true)]
    [string]$OldBinaryPath,
    [Parameter(Mandatory = $true)]
    [string]$NewBinaryPath,
    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,
    [string]$AttemptLabel = "attempt",
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = "Stop"

function Assert-File {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file not found: $Path"
    }
}

function Stop-TrackedProcess {
    param([System.Diagnostics.Process]$Process)
    if ($null -eq $Process) {
        return
    }
    try {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction Stop
        }
    }
    catch {
    }
}

function Wait-ProcessExitCode {
    param(
        [System.Diagnostics.Process]$Process,
        [int]$Timeout,
        [string]$Context
    )

    try {
        Wait-Process -InputObject $Process -Timeout $Timeout -ErrorAction Stop
    }
    catch {
        Stop-TrackedProcess -Process $Process
        throw "Timed out waiting for process exit: $Context"
    }
    $Process.Refresh()
    return $Process.ExitCode
}

function Start-Tg {
    param(
        [string]$Binary,
        [string[]]$CliArgs,
        [string]$Context
    )

    if ([string]::IsNullOrWhiteSpace($Binary)) {
        throw "Empty binary path in context: $Context"
    }
    if ($null -eq $CliArgs -or $CliArgs.Count -eq 0) {
        throw "Empty argument list in context: $Context"
    }
    $invalidArgs = @($CliArgs | Where-Object { $null -eq $_ -or [string]::IsNullOrWhiteSpace($_) })
    if ($invalidArgs.Count -gt 0) {
        throw "Invalid argument in context: $Context ; args=[$(($CliArgs | ForEach-Object { if ($null -eq $_) { '<null>' } elseif ([string]::IsNullOrWhiteSpace($_)) { '<empty>' } else { $_ } }) -join ', ')]"
    }

    $p = Start-Process -FilePath $Binary -ArgumentList $CliArgs -PassThru
    if ($null -eq $p) {
        throw "Failed to start process: $Context"
    }
    return $p
}

function Count-ReadyLines {
    param([string]$StatusLog)

    if (-not (Test-Path -LiteralPath $StatusLog -PathType Leaf)) {
        return 0
    }
    $lines = @(Get-Content -LiteralPath $StatusLog -ErrorAction SilentlyContinue)
    if ($null -eq $lines -or $lines.Count -eq 0) {
        return 0
    }
    return @($lines | Where-Object { $_ -eq "console-ready" }).Count
}

function Wait-Ready {
    param(
        [System.Diagnostics.Process]$Process,
        [string]$StatusLog,
        [int]$InitialReadyCount,
        [int]$Timeout,
        [string]$Context
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($Timeout)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Owner exited before ready line: $Context (exit $($Process.ExitCode))"
        }
        $count = Count-ReadyLines -StatusLog $StatusLog
        if ($count -gt $InitialReadyCount) {
            return $count
        }
        [System.Threading.Thread]::Sleep(150)
    }
    throw "Timed out waiting for console-ready: $Context"
}

function Invoke-Quit {
    param(
        [string]$Binary,
        [string]$Workdir,
        [int]$Timeout,
        [string]$Context
    )

    $p = Start-Tg -Binary $Binary -CliArgs @("-quit", "-workdir", $Workdir) -Context $Context
    return Wait-ProcessExitCode -Process $p -Timeout $Timeout -Context $Context
}

function New-WorkdirViews {
    param(
        [string]$Root,
        [string]$CellKey
    )

    $cellRoot = Join-Path $Root $CellKey
    $canonical = Join-Path $cellRoot "target"
    New-Item -ItemType Directory -Path $canonical -Force | Out-Null

    $alias = Join-Path $cellRoot "alias"
    if (Test-Path -LiteralPath $alias) {
        Remove-Item -LiteralPath $alias -Force -Recurse
    }
    $null = New-Item -ItemType Junction -Path $alias -Target $canonical -ErrorAction Stop

    $leaf = Split-Path -Leaf $canonical
    $caseLeaf = "TaRgEt"
    if ($leaf -eq $caseLeaf) {
        $caseLeaf = "TARGET"
    }
    $caseVariant = Join-Path (Split-Path -Parent $canonical) $caseLeaf

    return [ordered]@{
        CellRoot = $cellRoot
        Canonical = $canonical
        Alias = $alias
        CaseVariant = $caseVariant
        StatusLog = Join-Path $canonical "tdata\console_bootstrap.log"
    }
}

function Get-PathBySpelling {
    param(
        $Views,
        [string]$Spelling
    )

    switch ($Spelling) {
        "canonical" { return $Views.Canonical }
        "alias" { return $Views.Alias }
        "case" { return $Views.CaseVariant }
        default { throw "Unknown spelling: $Spelling" }
    }
}

function Get-UnexpectedTdataEntries {
    param([string]$CanonicalRoot)

    $tdata = Join-Path $CanonicalRoot "tdata"
    if (-not (Test-Path -LiteralPath $tdata -PathType Container)) {
        return @()
    }

    $allowed = @(
        "console_bootstrap.log",
        "tg_hosted_checkpoint.owner"
    )
    $entries = Get-ChildItem -LiteralPath $tdata -File -Force -ErrorAction SilentlyContinue
    if ($null -eq $entries) {
        return @()
    }
    return @($entries | Where-Object { $allowed -notcontains $_.Name } | ForEach-Object { $_.Name })
}

function Run-SequentialCell {
    param(
        [string]$CellName,
        [string]$OwnerBinary,
        [string]$SecondaryBinary,
        [string]$OwnerSpelling,
        [string]$SecondarySpelling,
        $Views,
        [int]$Timeout
    )

    $ownerPath = Get-PathBySpelling -Views $Views -Spelling $OwnerSpelling
    $secondaryPath = Get-PathBySpelling -Views $Views -Spelling $SecondarySpelling

    $readyBefore = Count-ReadyLines -StatusLog $Views.StatusLog
    $owner = $null
    $secondary = $null
    $ownerReadyCount = $readyBefore
    $secondaryExit = $null
    $ownerAliveAfterSecondary = $false
    $quitCanonical = $null
    $quitAlias = $null

    try {
        $owner = Start-Tg -Binary $OwnerBinary -CliArgs @("-console", "-workdir", $ownerPath) -Context "$CellName owner"
        $ownerReadyCount = Wait-Ready -Process $owner -StatusLog $Views.StatusLog -InitialReadyCount $readyBefore -Timeout $Timeout -Context "$CellName owner"

        $beforeSecondary = Count-ReadyLines -StatusLog $Views.StatusLog
        $secondary = Start-Tg -Binary $SecondaryBinary -CliArgs @("-console", "-console-exit", "-workdir", $secondaryPath) -Context "$CellName secondary"
        $secondaryExit = Wait-ProcessExitCode -Process $secondary -Timeout $Timeout -Context "$CellName secondary"
        $afterSecondary = Count-ReadyLines -StatusLog $Views.StatusLog

        $owner.Refresh()
        $ownerAliveAfterSecondary = -not $owner.HasExited

        $quitCanonical = Invoke-Quit -Binary $SecondaryBinary -Workdir $Views.Canonical -Timeout $Timeout -Context "$CellName quit canonical"
        $quitAlias = Invoke-Quit -Binary $SecondaryBinary -Workdir $Views.Alias -Timeout $Timeout -Context "$CellName quit alias"

        $ownerExit = Wait-ProcessExitCode -Process $owner -Timeout $Timeout -Context "$CellName owner quit"

        $unexpected = Get-UnexpectedTdataEntries -CanonicalRoot $Views.Canonical
        $secondaryLikelyBecameOwner = ($afterSecondary -gt $beforeSecondary)

        return [ordered]@{
            cell = $CellName
            mode = "sequential"
            ownerSpelling = $OwnerSpelling
            secondarySpelling = $SecondarySpelling
            ownerPid = $owner.Id
            ownerReadyCount = $ownerReadyCount
            secondaryExit = $secondaryExit
            ownerAliveAfterSecondary = $ownerAliveAfterSecondary
            quitCanonicalExit = $quitCanonical
            quitAliasExit = $quitAlias
            ownerExit = $ownerExit
            secondaryLikelyBecameOwner = $secondaryLikelyBecameOwner
            unexpectedTdataEntries = $unexpected
            assertions = [ordered]@{
                ownerAliveAfterSecondary = $ownerAliveAfterSecondary
                secondaryExitBounded = ($secondaryExit -ne $null)
                noUnexpectedTdataWrites = ($unexpected.Count -eq 0)
                noSecondaryOwnerPromotion = (-not $secondaryLikelyBecameOwner)
            }
        }
    }
    finally {
        Stop-TrackedProcess -Process $secondary
        Stop-TrackedProcess -Process $owner
    }
}

function Run-RaceCell {
    param(
        [string]$CellName,
        [string]$CanonicalBinary,
        [string]$AliasBinary,
        $Views,
        [int]$Timeout
    )

    $readyBefore = Count-ReadyLines -StatusLog $Views.StatusLog
    $pCanonical = $null
    $pAlias = $null

    try {
        $pCanonical = Start-Tg -Binary $CanonicalBinary -CliArgs @("-console", "-workdir", $Views.Canonical) -Context "$CellName canonical"
        $pAlias = Start-Tg -Binary $AliasBinary -CliArgs @("-console", "-workdir", $Views.Alias) -Context "$CellName alias"

        $deadline = [DateTime]::UtcNow.AddSeconds($Timeout)
        while ([DateTime]::UtcNow -lt $deadline) {
            $pCanonical.Refresh()
            $pAlias.Refresh()
            $readyCount = Count-ReadyLines -StatusLog $Views.StatusLog
            if ($readyCount -gt $readyBefore) {
                break
            }
            [System.Threading.Thread]::Sleep(120)
        }

        $pCanonical.Refresh()
        $pAlias.Refresh()
        $aliveCount = @($pCanonical, $pAlias | Where-Object { $null -ne $_ -and -not $_.HasExited }).Count

        $quitCanonical = Invoke-Quit -Binary $CanonicalBinary -Workdir $Views.Canonical -Timeout $Timeout -Context "$CellName quit canonical"
        $quitAlias = Invoke-Quit -Binary $AliasBinary -Workdir $Views.Alias -Timeout $Timeout -Context "$CellName quit alias"

        $canonExit = $null
        $aliasExit = $null
        if (-not $pCanonical.HasExited) {
            $canonExit = Wait-ProcessExitCode -Process $pCanonical -Timeout $Timeout -Context "$CellName canonical exit"
        }
        if (-not $pAlias.HasExited) {
            $aliasExit = Wait-ProcessExitCode -Process $pAlias -Timeout $Timeout -Context "$CellName alias exit"
        }

        $unexpected = Get-UnexpectedTdataEntries -CanonicalRoot $Views.Canonical

        return [ordered]@{
            cell = $CellName
            mode = "race"
            canonicalPid = $pCanonical.Id
            aliasPid = $pAlias.Id
            aliveCountBeforeQuit = $aliveCount
            quitCanonicalExit = $quitCanonical
            quitAliasExit = $quitAlias
            canonicalExit = $canonExit
            aliasExit = $aliasExit
            unexpectedTdataEntries = $unexpected
            assertions = [ordered]@{
                atMostOneAliveBeforeQuit = ($aliveCount -le 1)
                noUnexpectedTdataWrites = ($unexpected.Count -eq 0)
            }
        }
    }
    finally {
        Stop-TrackedProcess -Process $pCanonical
        Stop-TrackedProcess -Process $pAlias
    }
}

function Invoke-CellSafe {
    param(
        [string]$CellName,
        [scriptblock]$Body
    )

    Write-Host ("CELL_START=" + $CellName)
    try {
        $result = & $Body
        Write-Host ("CELL_DONE=" + $CellName)
        return $result
    }
    catch {
        Write-Host ("CELL_FAIL=" + $CellName + " ; " + $_.Exception.Message)
        return [ordered]@{
            cell = $CellName
            mode = "error"
            error = $_.Exception.Message
            assertions = [ordered]@{
                executionCompleted = $false
            }
        }
    }
}

Assert-File -Path $OldBinaryPath
Assert-File -Path $NewBinaryPath

$attemptRoot = Join-Path $OutputRoot $AttemptLabel
if (Test-Path -LiteralPath $attemptRoot) {
    Remove-Item -LiteralPath $attemptRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $attemptRoot -Force | Out-Null

$versionPairs = @(
    @{ key = "old_old"; owner = $OldBinaryPath; secondary = $OldBinaryPath },
    @{ key = "old_new"; owner = $OldBinaryPath; secondary = $NewBinaryPath },
    @{ key = "new_old"; owner = $NewBinaryPath; secondary = $OldBinaryPath },
    @{ key = "new_new"; owner = $NewBinaryPath; secondary = $NewBinaryPath }
)

$results = @()
foreach ($pair in $versionPairs) {
    $pairKey = $pair.key

    $views1 = New-WorkdirViews -Root $attemptRoot -CellKey "${pairKey}_c1"
    $results += Invoke-CellSafe -CellName "${pairKey}_c1" -Body {
        Run-SequentialCell -CellName "${pairKey}_c1" -OwnerBinary $pair.owner -SecondaryBinary $pair.secondary -OwnerSpelling "canonical" -SecondarySpelling "canonical" -Views $views1 -Timeout $TimeoutSeconds
    }

    $views2 = New-WorkdirViews -Root $attemptRoot -CellKey "${pairKey}_c2"
    $results += Invoke-CellSafe -CellName "${pairKey}_c2" -Body {
        Run-SequentialCell -CellName "${pairKey}_c2" -OwnerBinary $pair.owner -SecondaryBinary $pair.secondary -OwnerSpelling "canonical" -SecondarySpelling "alias" -Views $views2 -Timeout $TimeoutSeconds
    }

    $views3 = New-WorkdirViews -Root $attemptRoot -CellKey "${pairKey}_c3"
    $results += Invoke-CellSafe -CellName "${pairKey}_c3" -Body {
        Run-SequentialCell -CellName "${pairKey}_c3" -OwnerBinary $pair.owner -SecondaryBinary $pair.secondary -OwnerSpelling "alias" -SecondarySpelling "canonical" -Views $views3 -Timeout $TimeoutSeconds
    }

    $views4 = New-WorkdirViews -Root $attemptRoot -CellKey "${pairKey}_c4"
    $results += Invoke-CellSafe -CellName "${pairKey}_c4" -Body {
        Run-SequentialCell -CellName "${pairKey}_c4" -OwnerBinary $pair.owner -SecondaryBinary $pair.secondary -OwnerSpelling "case" -SecondarySpelling "canonical" -Views $views4 -Timeout $TimeoutSeconds
    }

    $views5 = New-WorkdirViews -Root $attemptRoot -CellKey "${pairKey}_c5"
    $results += Invoke-CellSafe -CellName "${pairKey}_c5" -Body {
        Run-SequentialCell -CellName "${pairKey}_c5" -OwnerBinary $pair.owner -SecondaryBinary $pair.secondary -OwnerSpelling "canonical" -SecondarySpelling "case" -Views $views5 -Timeout $TimeoutSeconds
    }

    $views6 = New-WorkdirViews -Root $attemptRoot -CellKey "${pairKey}_c6"
    $results += Invoke-CellSafe -CellName "${pairKey}_c6" -Body {
        Run-RaceCell -CellName "${pairKey}_c6" -CanonicalBinary $pair.owner -AliasBinary $pair.secondary -Views $views6 -Timeout $TimeoutSeconds
    }
}

$allAssertions = @()
foreach ($r in $results) {
    foreach ($entry in $r.assertions.GetEnumerator()) {
        $allAssertions += [pscustomobject]@{
            cell = $r.cell
            assertion = $entry.Key
            pass = [bool]$entry.Value
        }
    }
}

$failedAssertions = @($allAssertions | Where-Object { -not $_.pass })
$summary = [ordered]@{
    attempt = $AttemptLabel
    oldBinary = $OldBinaryPath
    newBinary = $NewBinaryPath
    outputRoot = $attemptRoot
    totalCells = $results.Count
    failedAssertions = $failedAssertions.Count
    timestampUtc = [DateTime]::UtcNow.ToString("o")
}

$report = [ordered]@{
    summary = $summary
    failed = $failedAssertions
    cells = $results
}

$jsonPath = Join-Path $attemptRoot "packet28_matrix_results.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

Write-Output "PACKET28_ATTEMPT=$AttemptLabel"
Write-Output "PACKET28_RESULTS_JSON=$jsonPath"
Write-Output "PACKET28_TOTAL_CELLS=$($summary.totalCells)"
Write-Output "PACKET28_FAILED_ASSERTIONS=$($summary.failedAssertions)"
if ($summary.failedAssertions -eq 0) {
    Write-Output "PACKET28_RESULT=PASS"
    exit 0
}
Write-Output "PACKET28_RESULT=FAIL"
exit 2
