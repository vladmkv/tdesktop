param(
    [string]$BinaryPath = "",
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

function Wait-ProcessExit {
    param(
        [System.Diagnostics.Process]$Process,
        [int]$Timeout,
        [string]$Context
    )

    try {
        Wait-Process -Id $Process.Id -Timeout $Timeout -ErrorAction Stop
    } catch {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        throw "$Context timed out after $Timeout seconds."
    }
    $Process.Refresh()
    return $Process.ExitCode
}

function Wait-ConsoleReady {
    param(
        [System.Diagnostics.Process]$Process,
        [string]$LogPath,
        [int]$Timeout,
        [int]$ExistingLineCount
    )

    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $Timeout) {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Hosted owner exited before becoming ready with code $($Process.ExitCode)."
        }
        if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
            $lines = @(Get-Content -LiteralPath $LogPath -ErrorAction Stop)
            if (($lines.Count -gt $ExistingLineCount) -and ($lines[-1] -eq "console-ready")) {
                return
            }
        }
        if ($Process.WaitForExit(100)) {
            throw "Hosted owner exited before becoming ready with code $($Process.ExitCode)."
        }
    }
    throw "Hosted owner did not report console-ready after $Timeout seconds."
}

if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
    $BinaryPath = Join-Path $repoRoot "out\Debug\tg.exe"
}

if (-not (Test-Path -LiteralPath $BinaryPath -PathType Leaf)) {
    throw "Debug binary not found: $BinaryPath`nBuild it with: cmake --build out --config Debug --target Telegram"
}

$demoRoot = Join-Path $env:TEMP ("tg-hosted-demo-" + [Guid]::NewGuid().ToString("N"))
$oneShotWorkdir = Join-Path $demoRoot "one-shot"
$ownershipWorkdir = Join-Path $demoRoot "ownership"
$oneShotStatusLog = Join-Path $oneShotWorkdir "tdata\console_bootstrap.log"
$ownershipStatusLog = Join-Path $ownershipWorkdir "tdata\console_bootstrap.log"
New-Item -ItemType Directory -Path $oneShotWorkdir -Force | Out-Null
New-Item -ItemType Directory -Path $ownershipWorkdir -Force | Out-Null

Write-Output "Telegram hosted console demo"
Write-Output "Binary:  $BinaryPath"
Write-Output "Demo data: $demoRoot"

Write-Output ""
Write-Output "[1/4] One-shot windowless startup"
$oneShotArguments = @(
    "-console",
    "-console-exit",
    "-workdir",
    $oneShotWorkdir
)
$oneShot = Start-Process -FilePath $BinaryPath -ArgumentList $oneShotArguments -PassThru
$oneShotExit = Wait-ProcessExit -Process $oneShot -Timeout $TimeoutSeconds -Context "One-shot checkpoint"
if ($oneShotExit -ne 0) {
    throw "One-shot checkpoint failed with exit code $oneShotExit. Workdir: $oneShotWorkdir"
}

if (-not (Test-Path -LiteralPath $oneShotStatusLog -PathType Leaf)) {
    throw "Hosted console checkpoint did not create its status log: $oneShotStatusLog"
}

$status = Get-Content -LiteralPath $oneShotStatusLog -ErrorAction Stop
if ($status -notcontains "console-ready") {
    throw "Expected 'console-ready' in status log. Actual output:`n$($status -join [Environment]::NewLine)"
}
Write-Output "      PASS: console-ready, exit code 0"

Write-Output "[2/4] Persistent windowless owner"
$existingStatusLineCount = 0
$owner = Start-Process -FilePath $BinaryPath -ArgumentList @(
    "-console",
    "-workdir",
    $ownershipWorkdir
) -PassThru

try {
    Wait-ConsoleReady `
        -Process $owner `
        -LogPath $ownershipStatusLog `
        -Timeout $TimeoutSeconds `
        -ExistingLineCount $existingStatusLineCount
    Write-Output "      PASS: owner PID $($owner.Id) holds the workdir"

    Write-Output "[3/4] Competing same-workdir instance"
    $secondary = Start-Process -FilePath $BinaryPath -ArgumentList @(
        "-console",
        "-console-exit",
        "-workdir",
        $ownershipWorkdir
    ) -PassThru
    $secondaryExit = Wait-ProcessExit -Process $secondary -Timeout $TimeoutSeconds -Context "Secondary instance"
    if ($secondaryExit -ne 0) {
        throw "Secondary instance failed with exit code $secondaryExit."
    }
    $owner.Refresh()
    if ($owner.HasExited) {
        throw "Primary owner exited while handling the secondary instance."
    }
    Write-Output "      PASS: secondary exited 0; owner PID $($owner.Id) remains active"

    Write-Output "[4/4] Controlled demo cleanup"
    Stop-Process -Id $owner.Id -Force -ErrorAction Stop
    try {
        Wait-Process -Id $owner.Id -Timeout $TimeoutSeconds -ErrorAction Stop
    } catch {
        throw "Hosted owner cleanup timed out after $TimeoutSeconds seconds."
    }
    $owner.Refresh()
    if (-not $owner.HasExited) {
        throw "Hosted owner is still running after cleanup."
    }
    Write-Output "      PASS: owner process removed"
} finally {
    $owner.Refresh()
    if (-not $owner.HasExited) {
        Stop-Process -Id $owner.Id -Force -ErrorAction SilentlyContinue
    }
}

Write-Output ""
Write-Output "Demo passed: windowless startup and exclusive ownership are working."
Write-Output "Disposable files remain at: $demoRoot"