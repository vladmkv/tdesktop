param(
    [string]$BinaryPath = "",
    [string]$DevProfile = "",
    [string]$EditChatId = "",
    [int]$EditMessageId = 0,
    [string]$EditText = "TG hosted console edit smoke",
    [string]$DeleteChatId = "",
    [int]$DeleteMessageId = 0,
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
    $logPath = Join-Path $env:TEMP ("tg-edit-delete-smoke-" + [Guid]::NewGuid().ToString("N") + ".log")
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

$invalidEdit = Invoke-ConsoleCommand @("edit", "user0", "1", "--text", "invalid peer")
if ($invalidEdit.ExitCode -eq 0 -or @($invalidEdit.Lines | Where-Object { $_ -eq "edit-error:invalid-peer" }).Count -ne 1) {
    throw "Invalid edit peer did not fail safely."
}
$missingConfirmation = Invoke-ConsoleCommand @("delete", "user0", "1")
if ($missingConfirmation.ExitCode -eq 0 -or @($missingConfirmation.Lines | Where-Object { $_ -eq "delete-error:missing-confirmation" }).Count -ne 1) {
    throw "One-shot delete without --yes did not fail safely."
}
$invalidDelete = Invoke-ConsoleCommand @("delete", "user0", "1", "--yes")
if ($invalidDelete.ExitCode -eq 0 -or @($invalidDelete.Lines | Where-Object { $_ -eq "delete-error:invalid-peer" }).Count -ne 1) {
    throw "Invalid confirmed delete did not fail safely."
}

if ([string]::IsNullOrWhiteSpace($EditChatId) -or $EditMessageId -le 0) {
    Write-Output "Edit/delete smoke passed: invalid IDs and missing one-shot confirmation made no mutation."
} else {
    $edit = Invoke-ConsoleCommand @("edit", $EditChatId, $EditMessageId, "--format", "json", "--text", $EditText)
    if ($edit.ExitCode -ne 0 -or @($edit.Lines | Where-Object { $_.StartsWith("edit-acknowledged:") }).Count -ne 1) {
        throw "Live edit did not receive a server acknowledgment."
    }
    $editJson = @($edit.Lines | Where-Object { $_.StartsWith("{") })
    if ($editJson.Count -ne 1 -or ($editJson[0] | ConvertFrom-Json).operation -ne "edit") {
        throw "Live edit did not emit one JSON acknowledgment record."
    }
}

if ([string]::IsNullOrWhiteSpace($DeleteChatId) -or $DeleteMessageId -le 0) {
    Write-Output "Live delete was not requested."
    exit 0
}

$delete = Invoke-ConsoleCommand @("delete", $DeleteChatId, $DeleteMessageId, "--format", "json", "--yes")
if ($delete.ExitCode -ne 0 -or @($delete.Lines | Where-Object { $_.StartsWith("delete-acknowledged:") }).Count -ne 1) {
    throw "Live delete did not receive a server acknowledgment."
}
$deleteJson = @($delete.Lines | Where-Object { $_.StartsWith("{") })
if ($deleteJson.Count -ne 1 -or ($deleteJson[0] | ConvertFrom-Json).operation -ne "delete") {
    throw "Live delete did not emit one JSON acknowledgment record."
}

Write-Output "Edit/delete smoke passed: server acknowledgments received for the identified test messages."