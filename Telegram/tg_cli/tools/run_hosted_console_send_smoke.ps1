param(
    [string]$BinaryPath = "",
    [string]$DevProfile = "",
    [string]$SendChatId = "",
    [string]$Text = "TG hosted console send smoke",
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
    throw "Close all Telegram processes before running this smoke."
}

function Invoke-ConsoleCommand([string[]]$Tokens) {
    $logPath = Join-Path $env:TEMP ("tg-send-smoke-" + [Guid]::NewGuid().ToString("N") + ".log")
    try {
        $arguments = @("-workdir", $DevProfile, "-console-log", $logPath, "-console-command") + $Tokens
        $process = Start-Process -FilePath $BinaryPath -ArgumentList $arguments -PassThru
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            throw "Console command timed out after $TimeoutSeconds seconds."
        }
        return @{ ExitCode = $process.ExitCode; Lines = @(Get-Content -LiteralPath $logPath -Encoding UTF8) }
    } finally {
        Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
    }
}

$invalid = Invoke-ConsoleCommand @("send", "user0", "--text", "invalid peer")
if ($invalid.ExitCode -eq 0 -or @($invalid.Lines | Where-Object { $_ -eq "send-error:invalid-peer" }).Count -ne 1) {
    throw "Invalid peer did not fail with send-error:invalid-peer."
}

if ([string]::IsNullOrWhiteSpace($SendChatId)) {
    Write-Output "Send smoke passed: invalid stable peer is rejected; no message was sent."
    Write-Output "To confirm a live send, rerun with -SendChatId user<your-dev-profile-self-id>."
    exit 0
}

$send = Invoke-ConsoleCommand @("send", $SendChatId, "--format", "json", "--text", $Text)
if ($send.ExitCode -ne 0 -or @($send.Lines | Where-Object { $_.StartsWith("send-acknowledged:") }).Count -ne 1) {
    throw "Live send did not receive a server acknowledgment."
}
if (@($send.Lines | Where-Object { $_ -match '^\{"chatId":' }).Count -ne 1) {
    throw "Live send did not emit one JSON acknowledgment record."
}

Write-Output "Send smoke passed: server acknowledgment received for $SendChatId."