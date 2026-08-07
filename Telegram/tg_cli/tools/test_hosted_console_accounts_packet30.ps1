param(
    [string]$BinaryPath = "",
    [string]$DevProfile = "",
    [string]$TempRoot = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path

if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
    $BinaryPath = Join-Path $RepoRoot "out\Debug\tg.exe"
}
if (-not (Test-Path -LiteralPath $BinaryPath)) {
    throw "Binary not found: $BinaryPath"
}

if ([string]::IsNullOrWhiteSpace($DevProfile)) {
    $DevProfile = Join-Path (Split-Path -Parent $RepoRoot) "tg-dev-profile"
}
if (-not (Test-Path -LiteralPath $DevProfile)) {
    throw "Dedicated dev profile does not exist: $DevProfile"
}

if ([string]::IsNullOrWhiteSpace($TempRoot)) {
    $TempRoot = Join-Path $env:TEMP ("tg_packet30_accounts_" + [Guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Path $TempRoot -Force | Out-Null

function Invoke-TgWithLog {
    param(
        [string[]]$CommandArgs,
        [string]$LogPath,
        [int]$ExpectedCode,
        [string]$Context
    )

    if (Test-Path -LiteralPath $LogPath) {
        Remove-Item -LiteralPath $LogPath -Force
    }

    $safeArgs = @($CommandArgs | Where-Object { $_ -ne $null -and $_ -ne "" })
    if ($safeArgs.Count -ne $CommandArgs.Count) {
        throw "Argument list for ${Context} contains null or empty values"
    }
    if ($safeArgs.Count -eq 0) {
        throw "Argument list for ${Context} is empty"
    }

    $quotedArgs = @($safeArgs | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + ($_ -replace '"', '\\"') + '"'
        } else {
            $_
        }
    })
    $argumentLine = [string]::Join(" ", $quotedArgs)

    $process = Start-Process -FilePath $BinaryPath -ArgumentList $argumentLine -PassThru
    if (-not $process.WaitForExit(60000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Timed out in ${Context}"
    }
    $process.WaitForExit()
    $code = $process.ExitCode
    if ($code -ne $ExpectedCode) {
        throw "Unexpected exit code $code in $Context (expected $ExpectedCode)"
    }
    if (-not (Test-Path -LiteralPath $LogPath)) {
        throw "Missing expected log file in ${Context}: $LogPath"
    }
    return Get-Content -LiteralPath $LogPath -Encoding UTF8
}

function Invoke-TgExitCode {
    param(
        [string[]]$CommandArgs,
        [int]$ExpectedCode,
        [string]$Context
    )

    $process = Start-Process -FilePath $BinaryPath -ArgumentList $CommandArgs -PassThru
    if (-not $process.WaitForExit(60000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Timed out in ${Context}"
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne $ExpectedCode) {
        throw "Unexpected exit code $($process.ExitCode) in $Context (expected $ExpectedCode)"
    }
}

function Get-MatchingLine {
    param(
        [string[]]$Lines,
        [string]$Prefix
    )

    $matchingLines = @($Lines | Where-Object { $_.StartsWith($Prefix) })
    if ($matchingLines.Count -ne 1) {
        throw "Expected one line with prefix '$Prefix', got $($matchingLines.Count)"
    }
    return $matchingLines[0]
}

function Assert-HasLine {
    param(
        [string[]]$Lines,
        [string]$Exact
    )

    if (-not ($Lines | Where-Object { $_ -eq $Exact })) {
        throw "Missing expected line: $Exact"
    }
}

$logOne = Join-Path $TempRoot "accounts_run1.log"
$linesOne = Invoke-TgWithLog -CommandArgs @("-console-accounts", "-workdir", $DevProfile, "-console-log", $logOne) -LogPath $logOne -ExpectedCode 0 -Context "accounts-run-1"

$logTwo = Join-Path $TempRoot "accounts_run2.log"
$linesTwo = Invoke-TgWithLog -CommandArgs @("-console-accounts", "-workdir", $DevProfile, "-console-log", $logTwo) -LogPath $logTwo -ExpectedCode 0 -Context "accounts-run-2"

Assert-HasLine -Lines $linesOne -Exact "accounts-mode:started"
Assert-HasLine -Lines $linesOne -Exact "accounts-mode:done"

$rowsOne = @($linesOne | Where-Object { $_.StartsWith("accounts-row:") })
$rowsTwo = @($linesTwo | Where-Object { $_.StartsWith("accounts-row:") })
if ($rowsOne.Count -ne $rowsTwo.Count) {
    throw "Account row counts differ between runs"
}
for ($i = 0; $i -lt $rowsOne.Count; $i++) {
    if ($rowsOne[$i] -ne $rowsTwo[$i]) {
        throw "Account row mismatch at index $i"
    }
}

$selectedOne = Get-MatchingLine -Lines $linesOne -Prefix "accounts-selected-storage-index:"
$selectedTwo = Get-MatchingLine -Lines $linesTwo -Prefix "accounts-selected-storage-index:"
if ($selectedOne -ne $selectedTwo) {
    throw "Selected storage index changed between runs"
}

$jsonLog = Join-Path $TempRoot "accounts_json.log"
$jsonLines = Invoke-TgWithLog -CommandArgs @("-console-accounts", "-console-format", "json", "-workdir", $DevProfile, "-console-log", $jsonLog) -LogPath $jsonLog -ExpectedCode 0 -Context "accounts-json"
$jsonObjectLine = @($jsonLines | Where-Object { $_.StartsWith("{") -and $_.EndsWith("}") })
if ($jsonObjectLine.Count -ne 1) {
    throw "Expected exactly one JSON object output line"
}
$json = $jsonObjectLine[0] | ConvertFrom-Json
foreach ($key in @("preflightStatus", "startResult", "accountCount", "authedCount", "activeStorageIndex", "selectedStorageIndex", "accounts")) {
    if (-not $json.PSObject.Properties.Name.Contains($key)) {
        throw "Missing JSON key: $key"
    }
}

Invoke-TgExitCode -CommandArgs @("-console-accounts", "-console-log", (Join-Path $TempRoot "neg_no_workdir.log")) -ExpectedCode 1 -Context "neg-no-workdir"

Invoke-TgExitCode -CommandArgs @("-console-accounts", "-workdir", $DevProfile) -ExpectedCode 1 -Context "neg-no-console-log"

$negIndexLog = Join-Path $TempRoot "neg_invalid_index.log"
$negIndexLines = Invoke-TgWithLog -CommandArgs @("-console-accounts", "-console-account-index", "999999", "-workdir", $DevProfile, "-console-log", $negIndexLog) -LogPath $negIndexLog -ExpectedCode 1 -Context "neg-invalid-index"
Assert-HasLine -Lines $negIndexLines -Exact "accounts-error:invalid-account-index"

$emptyProfile = Join-Path $TempRoot "empty_profile"
New-Item -ItemType Directory -Path $emptyProfile -Force | Out-Null
$emptyLog = Join-Path $TempRoot "empty_profile.log"
$emptyLines = Invoke-TgWithLog -CommandArgs @("-console-accounts", "-workdir", $emptyProfile, "-console-log", $emptyLog) -LogPath $emptyLog -ExpectedCode 1 -Context "neg-empty-profile"
Assert-HasLine -Lines $emptyLines -Exact "accounts-preflight-status:profile-not-found"

Invoke-TgExitCode -CommandArgs @("-console-accounts", "-console-owner-probe", "-workdir", $DevProfile, "-console-log", (Join-Path $TempRoot "invalid_arg_combo.log")) -ExpectedCode 1 -Context "neg-invalid-mode-combination"

Write-Output "HOSTED_CONSOLE_ACCOUNTS_PACKET30_TESTS=PASS"
