param(
    [string]$BinaryPath = "",
    [string]$DevProfile = "",
    [int]$TimeoutSeconds = 60,
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("accounts", "chats", "read", "send", "edit", "delete", "more", "help", "quit")]
    [string]$Command,
    [Parameter(Position = 1)]
    [string]$ChatId = "",
    [int]$Limit = 0,
    [string]$Cursor = "",
    [Parameter(Position = 2)]
    [int]$MessageId = 0,
    [string]$Text = "",
    [switch]$Yes,
    [ValidateSet("", "text", "json")]
    [string]$Format = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
    $BinaryPath = Join-Path $repoRoot "out\Debug\tg.exe"
}
if ([string]::IsNullOrWhiteSpace($DevProfile)) {
    $DevProfile = Join-Path (Split-Path -Parent $repoRoot) "tg-dev-profile"
}
if (($Command -in @("read", "send", "edit", "delete")) -and [string]::IsNullOrWhiteSpace($ChatId)) {
    throw "$Command requires -ChatId."
}
if (($Command -in @("edit", "delete")) -and $MessageId -lt 1) {
    throw "$Command requires -MessageId."
}
if (($Command -in @("send", "edit")) -and [string]::IsNullOrWhiteSpace($Text)) {
    throw "$Command requires -Text."
}
if ($Limit -lt 0) {
    throw "Limit must be positive."
}
if (-not (Test-Path -LiteralPath $BinaryPath -PathType Leaf)) {
    throw "Debug binary not found: $BinaryPath"
}
if (-not (Test-Path -LiteralPath (Join-Path $DevProfile "tdata") -PathType Container)) {
    throw "Dedicated dev profile not found: $DevProfile"
}

$running = @(Get-Process -Name tg -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    throw "Close all Telegram processes before using the command wrapper."
}

$logPath = Join-Path $env:TEMP ("tg-command-" + [Guid]::NewGuid().ToString("N") + ".log")
try {
    $commandTokens = @($Command)
    if (-not [string]::IsNullOrWhiteSpace($ChatId)) {
        $commandTokens += $ChatId
    }
    if ($MessageId -gt 0) {
        $commandTokens += "$MessageId"
    }
    if ($Limit -gt 0) {
        $commandTokens += @("--limit", "$Limit")
    }
    if (-not [string]::IsNullOrWhiteSpace($Cursor)) {
        $commandTokens += @("--cursor", $Cursor)
    }
    if (-not [string]::IsNullOrWhiteSpace($Text)) {
        $commandTokens += @("--text", $Text)
    }
    if ($Yes) {
        $commandTokens += "--yes"
    }
    if (-not [string]::IsNullOrWhiteSpace($Format)) {
        $commandTokens += @("--format", $Format)
    }
    $arguments = @("-workdir", $DevProfile, "-console-log", $logPath, "-console-command") + $commandTokens
    $process = Start-Process -FilePath $BinaryPath -ArgumentList $arguments -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Command timed out after $TimeoutSeconds seconds."
    }
    [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
    if (Test-Path -LiteralPath $logPath) {
        Get-Content -LiteralPath $logPath -Encoding UTF8
    } else {
        Write-Output "No command log was produced. Exit code: $($process.ExitCode)"
    }
    exit $process.ExitCode
} finally {
    Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
}