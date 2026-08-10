param(
    [string]$BinaryPath = "",
    [string]$DevProfile = "",
    [string]$PrivateChatId = "",
    [string]$ChannelChatId = "",
    [string]$NoProgressChatId = "",
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
if ($Limit -lt 1 -or $Limit -gt 100) {
    throw "Limit must be in range 1..100."
}
if (-not (Test-Path -LiteralPath $BinaryPath -PathType Leaf)) {
    throw "Debug binary not found: $BinaryPath"
}
if (-not (Test-Path -LiteralPath (Join-Path $DevProfile "tdata") -PathType Container)) {
    throw "Dedicated dev profile not found: $DevProfile"
}
$running = @(Get-Process -Name tg -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    throw "Close all Telegram processes before running this demo."
}

function Invoke-ReadCommand([string]$ChatId, [string]$LogPath, [string]$Cursor = "", [string]$Format = "") {
    $arguments = @(
        "-console-read", $ChatId, "-console-read-limit", "$Limit"
    )
    if (-not [string]::IsNullOrWhiteSpace($Cursor)) {
        $arguments += @("-console-read-cursor", $Cursor)
    }
    if (-not [string]::IsNullOrWhiteSpace($Format)) {
        $arguments += @("-console-format", $Format)
    }
    $arguments += @("-workdir", $DevProfile, "-console-log", $LogPath)
    $process = Start-Process -FilePath $BinaryPath -ArgumentList $arguments -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Read command for $ChatId timed out after $TimeoutSeconds seconds."
    }
    [void]$process.WaitForExit()
    return $process.ExitCode
}

$invalidLogPath = Join-Path $env:TEMP ("tg-read-invalid-" + [Guid]::NewGuid().ToString("N") + ".log")
$invalidLimitLogPath = Join-Path $env:TEMP ("tg-read-invalid-limit-" + [Guid]::NewGuid().ToString("N") + ".log")
$invalidCursorLogPath = Join-Path $env:TEMP ("tg-read-invalid-cursor-" + [Guid]::NewGuid().ToString("N") + ".log")
$noProgressLogPath = Join-Path $env:TEMP ("tg-read-no-progress-" + [Guid]::NewGuid().ToString("N") + ".log")
$privateLogPath = Join-Path $env:TEMP ("tg-read-private-" + [Guid]::NewGuid().ToString("N") + ".log")
$privateNextLogPath = Join-Path $env:TEMP ("tg-read-private-next-" + [Guid]::NewGuid().ToString("N") + ".log")
$privateJsonLogPath = Join-Path $env:TEMP ("tg-read-private-json-" + [Guid]::NewGuid().ToString("N") + ".log")
$channelLogPath = Join-Path $env:TEMP ("tg-read-channel-" + [Guid]::NewGuid().ToString("N") + ".log")
$channelNextLogPath = Join-Path $env:TEMP ("tg-read-channel-next-" + [Guid]::NewGuid().ToString("N") + ".log")
$channelJsonLogPath = Join-Path $env:TEMP ("tg-read-channel-json-" + [Guid]::NewGuid().ToString("N") + ".log")

try {
    $readSources = @(
        (Join-Path $repoRoot "Telegram\tg_cli\hosted\hosted_console_read_mode.cpp")
        (Join-Path $repoRoot "Telegram\tg_cli\hosted\hosted_console_read_format.cpp")
    )
    $forbiddenReadApis = '\b(readInbox|readInboxTill|sendPendingReadInbox|download\w*|save\w*)\s*\('
    $forbiddenMatches = Select-String -LiteralPath $readSources -Pattern $forbiddenReadApis
    if ($forbiddenMatches) {
        throw "Hosted read sources call a forbidden read/download/save API: $($forbiddenMatches[0].Line.Trim())"
    }

    if ((Invoke-ReadCommand "user0" $invalidLogPath) -ne 1) {
        throw "Invalid peer did not exit 1."
    }
    $invalidLines = Get-Content -LiteralPath $invalidLogPath -Encoding UTF8
    if ($invalidLines -notcontains "read-error:invalid-peer") {
        throw "Invalid peer did not report read-error:invalid-peer."
    }

    $invalidLimitProcess = Start-Process -FilePath $BinaryPath -ArgumentList @(
        "-console-read", "user0", "-console-read-limit", "0", "-workdir", $DevProfile, "-console-log", $invalidLimitLogPath
    ) -PassThru
    if (-not $invalidLimitProcess.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $invalidLimitProcess.Id -Force -ErrorAction SilentlyContinue
        throw "Invalid-limit read command timed out after $TimeoutSeconds seconds."
    }
    if ($invalidLimitProcess.ExitCode -ne 1) {
        throw "Invalid read limit exited $($invalidLimitProcess.ExitCode), expected 1."
    }

    $cursorPeer = if (-not [string]::IsNullOrWhiteSpace($PrivateChatId)) {
        $PrivateChatId
    } else {
        $ChannelChatId
    }
    if (-not [string]::IsNullOrWhiteSpace($cursorPeer)) {
        if ((Invoke-ReadCommand $cursorPeer $invalidCursorLogPath "v1:$cursorPeer:1:0") -ne 1) {
            throw "Invalid cursor did not exit 1."
        }
        $invalidCursorLines = Get-Content -LiteralPath $invalidCursorLogPath -Encoding UTF8
        if ($invalidCursorLines -notcontains "read-error:invalid-cursor") {
            throw "Invalid cursor did not report read-error:invalid-cursor."
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($NoProgressChatId)) {
        if ((Invoke-ReadCommand $NoProgressChatId $noProgressLogPath) -ne 1) {
            throw "Controlled no-progress read did not exit 1."
        }
        $noProgressLines = Get-Content -LiteralPath $noProgressLogPath -Encoding UTF8
        if ($noProgressLines -notcontains "read-error:history-no-progress") {
            throw "Controlled no-progress read did not report history-no-progress."
        }
    } else {
        Write-Output "Skipped no-progress read: no controlled no-progress chat ID supplied."
    }

    foreach ($entry in @(
        @{ Id = $PrivateChatId; Log = $privateLogPath; NextLog = $privateNextLogPath; JsonLog = $privateJsonLogPath; Kind = "private" },
        @{ Id = $ChannelChatId; Log = $channelLogPath; NextLog = $channelNextLogPath; JsonLog = $channelJsonLogPath; Kind = "channel" }
    )) {
        if ([string]::IsNullOrWhiteSpace($entry.Id)) {
            Write-Output "Skipped $($entry.Kind) read: no stable chat ID supplied."
            continue
        }
        if ((Invoke-ReadCommand $entry.Id $entry.Log) -ne 0) {
            throw "$($entry.Kind) read failed for $($entry.Id)."
        }
        $countLine = @(Get-Content -LiteralPath $entry.Log -Encoding UTF8 | Where-Object { $_.StartsWith("read-count:") })
        if ($countLine.Count -ne 1) {
            throw "$($entry.Kind) read did not emit exactly one read-count line."
        }
        if ($countLine[0] -eq "read-count:0") {
            throw "$($entry.Kind) read returned no messages."
        }
        $rows = @(Get-Content -LiteralPath $entry.Log -Encoding UTF8 | Where-Object { $_.StartsWith("read-row:") })
        $cursorLine = @(Get-Content -LiteralPath $entry.Log -Encoding UTF8 | Where-Object { $_.StartsWith("read-next-cursor:") })
        if ($rows.Count -ne [int]($countLine[0].Substring("read-count:".Length)) -or $cursorLine.Count -ne 1) {
            throw "$($entry.Kind) read did not emit matching rows and one next cursor."
        }
        $cursor = $cursorLine[0].Substring("read-next-cursor:".Length)
        if ([string]::IsNullOrWhiteSpace($cursor)) {
            throw "$($entry.Kind) read did not return a usable next cursor."
        }
        if ((Invoke-ReadCommand $entry.Id $entry.NextLog $cursor) -ne 0) {
            throw "$($entry.Kind) next-page read failed for $($entry.Id)."
        }
        $nextRows = @(Get-Content -LiteralPath $entry.NextLog -Encoding UTF8 | Where-Object { $_.StartsWith("read-row:") })
        $firstIds = @($rows | ForEach-Object { ($_ -split '\|')[1] })
        $nextIds = @($nextRows | ForEach-Object { ($_ -split '\|')[1] })
        if (@($firstIds | Where-Object { $nextIds -contains $_ }).Count -ne 0) {
            throw "$($entry.Kind) pages overlap message IDs."
        }
        if ((Invoke-ReadCommand $entry.Id $entry.JsonLog "" "json") -ne 0) {
            throw "$($entry.Kind) JSON read failed for $($entry.Id)."
        }
        $jsonLine = @(Get-Content -LiteralPath $entry.JsonLog -Encoding UTF8 | Where-Object { $_.StartsWith("{") })
        if ($jsonLine.Count -ne 1) {
            throw "$($entry.Kind) JSON read returned $($jsonLine.Count) JSON lines, expected one."
        }
        $jsonOutput = $jsonLine[0] | ConvertFrom-Json
        if ($jsonOutput.chatId -ne $entry.Id -or $jsonOutput.requestedLimit -ne $Limit -or $jsonOutput.count -ne $jsonOutput.messages.Count -or [string]::IsNullOrWhiteSpace($jsonOutput.nextCursor)) {
            throw "$($entry.Kind) JSON read did not preserve the read contract."
        }
        foreach ($message in $jsonOutput.messages) {
            if ($null -eq $message.id -or $null -eq $message.date -or $null -eq $message.text -or $null -eq $message.mediaType -or $null -eq $message.mediaName -or $null -eq $message.mediaSize) {
                throw "$($entry.Kind) JSON read omitted a message field."
            }
        }
    }
} finally {
    Remove-Item -LiteralPath $invalidLogPath,$invalidLimitLogPath,$invalidCursorLogPath,$noProgressLogPath,$privateLogPath,$privateNextLogPath,$privateJsonLogPath,$channelLogPath,$channelNextLogPath,$channelJsonLogPath -Force -ErrorAction SilentlyContinue
}

Write-Output "Telegram CLI read demo"
Write-Output "Read safety, strict cursor, text/JSON contract, and non-overlapping successor-page checks verified."