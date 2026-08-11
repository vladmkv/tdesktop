param(
    [string]$BinaryPath = "",
    [string]$DevProfile = "",
    [string]$ReadChatId = "user3527271",
    [int]$TimeoutSeconds = 90
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

$logPath = Join-Path $env:TEMP ("tg-repl-demo-" + [Guid]::NewGuid().ToString("N") + ".log")
try {
    $commands = @(
        ("not-a-command-" + [char]0x00E9),
        "delete user0 1",
        "cancel",
        "accounts",
        "chats --limit 3",
        "read $ReadChatId --limit 3",
        "more",
        "help",
        "quit"
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $BinaryPath
    $startInfo.Arguments = "-workdir `"$DevProfile`" -console-log `"$logPath`" -console-repl"
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $inputBytes = [System.Text.UTF8Encoding]::new($false).GetBytes(($commands -join "`n") + "`n")
    $process.StandardInput.BaseStream.Write($inputBytes, 0, $inputBytes.Length)
    $process.StandardInput.BaseStream.Flush()
    $process.StandardInput.Close()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "REPL timed out after $TimeoutSeconds seconds."
    }
    if ($process.ExitCode -ne 0) {
        throw "REPL failed with exit code $($process.ExitCode)."
    }
    $lines = @(Get-Content -LiteralPath $logPath -Encoding UTF8)
    foreach ($marker in @(
        "repl-started",
        "command-error:unknown-command:not-a-command-",
        "delete-confirmation-required:user0:1",
        "delete-cancelled",
        "accounts-mode:done",
        "chats-count:3",
        "command-help:",
        "command-quit:requested")) {
        if (@($lines | Where-Object { $_.StartsWith($marker) }).Count -lt 1) {
            throw "REPL did not emit expected marker: $marker"
        }
    }
    if (@($lines | Where-Object { $_ -eq "read-mode:started" }).Count -ne 2) {
        throw "REPL did not dispatch exactly two reads for read followed by more."
    }
    $readCursors = @($lines | Where-Object { $_.StartsWith("read-next-cursor:") })
    if ($readCursors.Count -ne 2 -or $readCursors[0] -eq "read-next-cursor:") {
        throw "REPL did not preserve a successful read cursor for more."
    }
    if (@($lines | Where-Object { $_ -eq "accounts-mode:done" }).Count -ne 1) {
        throw "Malformed input ended the REPL before accounts dispatched."
    }
} finally {
    Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
}

Write-Output "Telegram CLI hosted REPL demo"
Write-Output "Read chat: $ReadChatId"
Write-Output "Demo passed: malformed input recovered; accounts, chats, read, more, help, and quit completed."