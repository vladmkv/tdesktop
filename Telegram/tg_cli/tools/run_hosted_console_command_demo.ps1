param(
    [string]$BinaryPath = "",
    [string]$DevProfile = "",
    [string]$ReadChatId = "user3527271",
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
    $BinaryPath = Join-Path $repoRoot "out\Debug\tg.exe"
}
if ([string]::IsNullOrWhiteSpace($DevProfile)) {
    $DevProfile = Join-Path (Split-Path -Parent $repoRoot) "tg-dev-profile"
}
if (-not (Test-Path -LiteralPath $BinaryPath -PathType Leaf)) {
    throw "Debug binary not found: $BinaryPath"
}
if (-not (Test-Path -LiteralPath (Join-Path $DevProfile "tdata") -PathType Container)) {
    throw "Dedicated dev profile not found: $DevProfile"
}
if (@(Get-Process -Name tg -ErrorAction SilentlyContinue).Count -gt 0) {
    throw "Close all Telegram processes before running this demo."
}

$logs = @()
try {
    $commands = @(
        @{ Tokens = @("accounts"); Expected = "accounts-mode:done" },
        @{ Tokens = @("chats", "--", "--limit", "3"); Expected = "chats-count:3" },
        @{ Tokens = @("read", "--", $ReadChatId, "--limit", "3"); Expected = "read-count:" },
        @{ Tokens = @("help"); Expected = "command-help:" },
        @{ Tokens = @("more"); Expected = "command-more:no-previous-read" },
        @{ Tokens = @("quit"); Expected = "command-quit:requested" }
    )

    foreach ($command in $commands) {
        $logPath = Join-Path $env:TEMP ("tg-command-demo-" + [Guid]::NewGuid().ToString("N") + ".log")
        $logs += $logPath
        $arguments = @("-workdir", $DevProfile, "-console-log", $logPath, "-console-command") + $command.Tokens
        $process = Start-Process -FilePath $BinaryPath -ArgumentList $arguments -PassThru
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            throw "Command $($command.Tokens[0]) timed out after $TimeoutSeconds seconds."
        }
        if ($process.ExitCode -ne 0) {
            throw "Command $($command.Tokens[0]) failed with exit code $($process.ExitCode)."
        }
        $lines = @(Get-Content -LiteralPath $logPath -Encoding UTF8)
        if ($command.Expected -eq "read-count:") {
            if (@($lines | Where-Object { $_.StartsWith($command.Expected) -and $_ -ne "read-count:0" }).Count -ne 1) {
                throw "Command read did not return a non-empty page."
            }
        } elseif (@($lines | Where-Object { $_.StartsWith($command.Expected) }).Count -ne 1) {
            throw "Command $($command.Tokens[0]) did not emit $($command.Expected)."
        }
    }
} finally {
    Remove-Item -LiteralPath $logs -Force -ErrorAction SilentlyContinue
}

Write-Output "Telegram CLI command dispatcher demo"
Write-Output "Read chat: $ReadChatId"
Write-Output "Demo passed: accounts, chats, read, more, help, and quit dispatched successfully."