param(
    [string]$BinaryPath = "",
    [string]$DevProfile = "",
    [int]$Limit = 3,
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
if ($Limit -lt 1 -or $Limit -gt 1000) {
    throw "Limit must be in range 1..1000."
}
if (-not (Test-Path -LiteralPath $BinaryPath -PathType Leaf)) {
    throw "Debug binary not found: $BinaryPath"
}
if (-not (Test-Path -LiteralPath (Join-Path $DevProfile "tdata") -PathType Container)) {
    throw "Dedicated dev profile not found: $DevProfile"
}

$running = @(Get-Process -Name tg -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -eq $BinaryPath
})
if ($running.Count -gt 0) {
    throw "Close the Debug Telegram window before running this demo."
}

$textLogPath = Join-Path $env:TEMP ("tg-chats-demo-" + [Guid]::NewGuid().ToString("N") + ".log")
$jsonLogPath = Join-Path $env:TEMP ("tg-chats-demo-" + [Guid]::NewGuid().ToString("N") + ".log")
$invalidLogPath = Join-Path $env:TEMP ("tg-chats-demo-" + [Guid]::NewGuid().ToString("N") + ".log")

try {
    $textProcess = Start-Process -FilePath $BinaryPath -ArgumentList @(
        "-console-chats", "-console-chats-limit", "$Limit", "-workdir", $DevProfile, "-console-log", $textLogPath
    ) -PassThru
    if (-not $textProcess.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $textProcess.Id -Force -ErrorAction SilentlyContinue
        throw "Text chats command timed out after $TimeoutSeconds seconds."
    }
    if ($textProcess.ExitCode -ne 0) {
        throw "Text chats command failed with exit code $($textProcess.ExitCode)."
    }
    $textRows = @(Get-Content -LiteralPath $textLogPath -Encoding UTF8 | Where-Object { $_.StartsWith("chats-row:") })
    if ($textRows.Count -ne $Limit) {
        throw "Text chats command returned $($textRows.Count) rows, expected $Limit."
    }

    $jsonProcess = Start-Process -FilePath $BinaryPath -ArgumentList @(
        "-console-chats", "-console-chats-limit", "$Limit", "-console-format", "json", "-workdir", $DevProfile, "-console-log", $jsonLogPath
    ) -PassThru
    if (-not $jsonProcess.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $jsonProcess.Id -Force -ErrorAction SilentlyContinue
        throw "JSON chats command timed out after $TimeoutSeconds seconds."
    }
    if ($jsonProcess.ExitCode -ne 0) {
        throw "JSON chats command failed with exit code $($jsonProcess.ExitCode)."
    }
    $jsonLine = @(Get-Content -LiteralPath $jsonLogPath -Encoding UTF8 | Where-Object { $_.StartsWith("{") })
    if ($jsonLine.Count -ne 1) {
        throw "JSON chats command returned $($jsonLine.Count) JSON lines, expected one."
    }
    $jsonOutput = $jsonLine[0] | ConvertFrom-Json
    if ($jsonOutput.requestedLimit -ne $Limit -or $jsonOutput.count -ne $Limit -or $jsonOutput.chats.Count -ne $Limit) {
        throw "JSON chats command did not preserve the requested limit."
    }

    $invalidProcess = Start-Process -FilePath $BinaryPath -ArgumentList @(
        "-console-chats", "-console-chats-limit", "0", "-workdir", $DevProfile, "-console-log", $invalidLogPath
    ) -PassThru
    if (-not $invalidProcess.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $invalidProcess.Id -Force -ErrorAction SilentlyContinue
        throw "Invalid-limit chats command timed out after $TimeoutSeconds seconds."
    }
    if ($invalidProcess.ExitCode -ne 1) {
        throw "Invalid chats limit exited $($invalidProcess.ExitCode), expected 1."
    }
} finally {
    Remove-Item -LiteralPath $textLogPath,$jsonLogPath,$invalidLogPath -Force -ErrorAction SilentlyContinue
}

Write-Output "Telegram CLI chats demo"
Write-Output "Profile: $DevProfile"
Write-Output "Rows: $Limit"
Write-Output "Demo passed: text, JSON, and invalid-limit behaviors verified."