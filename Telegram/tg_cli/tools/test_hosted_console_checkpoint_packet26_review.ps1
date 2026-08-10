param(
    [string]$BinaryPath = "",
    [string]$TempRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
    $BinaryPath = Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path "out\Debug\tg.exe"
}
if (-not (Test-Path -LiteralPath $BinaryPath)) {
    throw "Binary not found: $BinaryPath"
}
$BinaryPath = (Resolve-Path -LiteralPath $BinaryPath).Path
if ([string]::IsNullOrWhiteSpace($TempRoot)) {
    $TempRoot = Join-Path $env:TEMP ("tg_packet26_review_" + [Guid]::NewGuid().ToString("N"))
}

New-Item -ItemType Directory -Path $TempRoot -Force | Out-Null

function Start-TgProcess {
    param(
        [string[]]$CliArgs,
        [string]$Context
    )

    $process = Start-Process -FilePath $BinaryPath -ArgumentList $CliArgs -PassThru
    if ($null -eq $process) {
        throw "Failed to start tg process: $Context"
    }
    return $process
}

function Stop-TgProcessIfRunning {
    param([System.Diagnostics.Process]$Process)

    if ($null -eq $Process) {
        return
    }
    try {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction Stop
        }
    } catch {
    }
}

function Get-HostedWorkdirDiagnostics {
    param([string]$Workdir)

    if ([string]::IsNullOrWhiteSpace($Workdir)) {
        return "workdir=<none>"
    }

    $tdata = Join-Path $Workdir "tdata"
    $marker = Join-Path $tdata "tg_hosted_checkpoint.owner"
    $runtimeLock = Join-Path $Workdir "tg_hosted_checkpoint.runtime.lock"
    $initLock = Join-Path $Workdir "tg_hosted_checkpoint.init.lock"
    $statusLog = Join-Path $tdata "console_bootstrap.log"

    $lines = @()
    $lines += "workdir=$Workdir"
    $lines += "tdata_exists=$([bool](Test-Path -LiteralPath $tdata))"
    $lines += "marker_exists=$([bool](Test-Path -LiteralPath $marker))"
    $lines += "runtime_lock_exists=$([bool](Test-Path -LiteralPath $runtimeLock))"
    $lines += "init_lock_exists=$([bool](Test-Path -LiteralPath $initLock))"
    $lines += "status_log_exists=$([bool](Test-Path -LiteralPath $statusLog))"

    if (Test-Path -LiteralPath $statusLog) {
        $tail = Get-Content -LiteralPath $statusLog -Tail 20 -ErrorAction SilentlyContinue
        if ($null -ne $tail -and $tail.Count -gt 0) {
            $lines += "status_log_tail="
            $lines += ($tail | ForEach-Object { "  $_" })
        }
    }

    return ($lines -join [Environment]::NewLine)
}

function Wait-TgExitCode {
    param(
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds,
        [string]$Context,
        [string]$Workdir = ""
    )

    try {
        Wait-Process -InputObject $Process -Timeout $TimeoutSeconds -ErrorAction Stop
    } catch {
        Stop-TgProcessIfRunning -Process $Process
        $diag = Get-HostedWorkdirDiagnostics -Workdir $Workdir
        throw "Timed out after ${TimeoutSeconds}s while waiting for process exit ($Context).`n$diag"
    }
    $Process.Refresh()
    return $Process.ExitCode
}

function Invoke-TgExpectedExit {
    param(
        [string[]]$CliArgs,
        [int[]]$ExpectedExit,
        [int]$TimeoutSeconds,
        [string]$Context,
        [string]$Workdir = ""
    )

    $process = Start-TgProcess -CliArgs $CliArgs -Context $Context
    $code = Wait-TgExitCode -Process $process -TimeoutSeconds $TimeoutSeconds -Context $Context -Workdir $Workdir
    if ($ExpectedExit -notcontains $code) {
        $diag = Get-HostedWorkdirDiagnostics -Workdir $Workdir
        throw "Unexpected exit code $code (expected $($ExpectedExit -join ', ')) in $Context, args: $($CliArgs -join ' ')`n$diag"
    }
    return $code
}

function Wait-HostedConsoleReady {
    param(
        [System.Diagnostics.Process]$OwnerProcess,
        [string]$Workdir,
        [int]$TimeoutSeconds,
        [string]$RequiredLine = "console-ready"
    )

    $statusLog = Join-Path (Join-Path $Workdir "tdata") "console_bootstrap.log"
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $OwnerProcess.Refresh()
        if ($OwnerProcess.HasExited) {
            $diag = Get-HostedWorkdirDiagnostics -Workdir $Workdir
            throw "Hosted owner exited before readiness line ($RequiredLine).`n$diag"
        }

        if (Test-Path -LiteralPath $statusLog) {
            $content = Get-Content -LiteralPath $statusLog -ErrorAction SilentlyContinue
            if ($null -ne $content -and ($content | Where-Object { $_ -eq $RequiredLine })) {
                return $statusLog
            }
        }

        [System.Threading.Thread]::Sleep(200)
    }

    Stop-TgProcessIfRunning -Process $OwnerProcess
    $diag = Get-HostedWorkdirDiagnostics -Workdir $Workdir
    throw "Timed out after ${TimeoutSeconds}s waiting for hosted readiness line '$RequiredLine'.`n$diag"
}

function New-DisposableWorkdir {
    param([string]$Name)

    $path = Join-Path $TempRoot $Name
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    return $path
}

function New-JunctionAlias {
    param(
        [string]$Name,
        [string]$TargetPath
    )

    $aliasPath = Join-Path $TempRoot $Name
    if (Test-Path -LiteralPath $aliasPath) {
        Remove-Item -LiteralPath $aliasPath -Force -Recurse
    }
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $aliasPath) -Force
    try {
        $null = New-Item -ItemType Junction -Path $aliasPath -Target $TargetPath -ErrorAction Stop
    } catch {
        return ""
    }
    if (-not (Test-Path -LiteralPath $aliasPath)) {
        return ""
    }
    return $aliasPath
}

$markerRelative = "tdata\tg_hosted_checkpoint.owner"
$defaultMarkerPath = Join-Path (Split-Path -Parent $BinaryPath) $markerRelative
$defaultMarkerExistedBefore = Test-Path -LiteralPath $defaultMarkerPath

try {
    Invoke-TgExpectedExit -CliArgs @("-console", "-console-exit") -ExpectedExit @(1) -TimeoutSeconds 45 -Context "console-without-explicit-workdir"
    $defaultMarkerExistedAfter = Test-Path -LiteralPath $defaultMarkerPath
    if ((-not $defaultMarkerExistedBefore) -and $defaultMarkerExistedAfter) {
        throw "Console without explicit -workdir touched default profile marker: $defaultMarkerPath"
    }

    $fakeDesktop = New-DisposableWorkdir -Name "fake_desktop"
    $fakeTdata = Join-Path $fakeDesktop "tdata"
    New-Item -ItemType Directory -Path $fakeTdata -Force | Out-Null
    Set-Content -Path (Join-Path $fakeTdata "D877F783D5D3EF8C") -Value "fake" -NoNewline
    Invoke-TgExpectedExit -CliArgs @("-console", "-console-exit", "-workdir", $fakeDesktop) -ExpectedExit @(1) -TimeoutSeconds 45 -Context "fake-desktop-profile-rejected" -Workdir $fakeDesktop

    $sharedWorkdir = New-DisposableWorkdir -Name "shared_hosted"
    $first = Start-TgProcess -CliArgs @("-console", "-workdir", $sharedWorkdir) -Context "shared-workdir-owner"
    Wait-HostedConsoleReady -OwnerProcess $first -Workdir $sharedWorkdir -TimeoutSeconds 60 | Out-Null
    Invoke-TgExpectedExit -CliArgs @("-console", "-console-exit", "-workdir", $sharedWorkdir) -ExpectedExit @(0) -TimeoutSeconds 45 -Context "shared-workdir-secondary" -Workdir $sharedWorkdir
    Invoke-TgExpectedExit -CliArgs @("-quit", "-workdir", $sharedWorkdir) -ExpectedExit @(0) -TimeoutSeconds 45 -Context "shared-workdir-quit-owner" -Workdir $sharedWorkdir
    try {
        Wait-TgExitCode -Process $first -TimeoutSeconds 30 -Context "shared-workdir-owner-exit" -Workdir $sharedWorkdir | Out-Null
    } catch {
        Stop-TgProcessIfRunning -Process $first
        throw
    }

    $invalidLogWorkdir = New-DisposableWorkdir -Name "invalid_console_log"
    $invalidLogPath = Join-Path $invalidLogWorkdir "tdata"
    New-Item -ItemType Directory -Path $invalidLogPath -Force | Out-Null
    Invoke-TgExpectedExit -CliArgs @("-console", "-console-exit", "-workdir", $invalidLogWorkdir, "-console-log", $invalidLogPath) -ExpectedExit @(1) -TimeoutSeconds 45 -Context "invalid-console-log-path" -Workdir $invalidLogWorkdir

    $h1Workdir = New-DisposableWorkdir -Name "h1_one_shot"
    Invoke-TgExpectedExit -CliArgs @("-console", "-console-exit", "-workdir", $h1Workdir) -ExpectedExit @(0) -TimeoutSeconds 45 -Context "h1-one-shot" -Workdir $h1Workdir

    $h2Workdir = New-DisposableWorkdir -Name "h2_persistent"
    $h2First = Start-TgProcess -CliArgs @("-console", "-workdir", $h2Workdir) -Context "h2-owner"
    Wait-HostedConsoleReady -OwnerProcess $h2First -Workdir $h2Workdir -TimeoutSeconds 60 | Out-Null
    Invoke-TgExpectedExit -CliArgs @("-console", "-console-exit", "-workdir", $h2Workdir) -ExpectedExit @(0) -TimeoutSeconds 45 -Context "h2-secondary" -Workdir $h2Workdir
    Invoke-TgExpectedExit -CliArgs @("-quit", "-workdir", $h2Workdir) -ExpectedExit @(0) -TimeoutSeconds 45 -Context "h2-quit-owner" -Workdir $h2Workdir
    try {
        Wait-TgExitCode -Process $h2First -TimeoutSeconds 30 -Context "h2-owner-exit" -Workdir $h2Workdir | Out-Null
    } catch {
        Stop-TgProcessIfRunning -Process $h2First
        throw
    }

    $aliasTarget = New-DisposableWorkdir -Name "alias_target"
    $aliasPath = New-JunctionAlias -Name "alias_view" -TargetPath $aliasTarget
    if ([string]::IsNullOrWhiteSpace($aliasPath)) {
        throw "Failed to create junction alias for alias-ownership probe"
    }
    $aliasOwner = Start-TgProcess -CliArgs @("-console", "-workdir", $aliasPath) -Context "alias-owner"
    Wait-HostedConsoleReady -OwnerProcess $aliasOwner -Workdir $aliasTarget -TimeoutSeconds 60 | Out-Null
    Invoke-TgExpectedExit -CliArgs @("-quit", "-workdir", $aliasTarget) -ExpectedExit @(0) -TimeoutSeconds 45 -Context "alias-secondary-quit-handshake" -Workdir $aliasTarget
    $aliasOwner.Refresh()
    if ($aliasOwner.HasExited) {
        throw "Alias owner exited unexpectedly during alias ownership probe"
    }

    Write-Output "HOSTED_CHECKPOINT_PACKET26_REVIEW_TESTS=PASS"
}
finally {
    Get-Process -Name tg -ErrorAction SilentlyContinue | ForEach-Object {
        if ($_.Path -eq $BinaryPath) {
            try { Stop-Process -Id $_.Id -Force -ErrorAction Stop } catch {}
        }
    }
}
