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
if ([string]::IsNullOrWhiteSpace($TempRoot)) {
    $TempRoot = Join-Path $env:TEMP ("tg_packet26_review_" + [Guid]::NewGuid().ToString("N"))
}

New-Item -ItemType Directory -Path $TempRoot -Force | Out-Null

function Invoke-Tg {
    param(
        [string[]]$CliArgs,
        [int]$ExpectedExit
    )

    $process = Start-Process -FilePath $BinaryPath -ArgumentList $CliArgs -PassThru -Wait
    $code = $process.ExitCode
    if ($code -ne $ExpectedExit) {
        throw "Unexpected exit code $code, expected $ExpectedExit, args: $($CliArgs -join ' ')"
    }
}

function Invoke-TgExitCode {
    param([string[]]$CliArgs)

    $process = Start-Process -FilePath $BinaryPath -ArgumentList $CliArgs -PassThru -Wait
    return $process.ExitCode
}

function New-DisposableWorkdir {
    param([string]$Name)

    $path = Join-Path $TempRoot $Name
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    return $path
}

$markerRelative = "tdata\tg_hosted_checkpoint.owner"
$defaultMarkerPath = Join-Path (Split-Path -Parent $BinaryPath) $markerRelative
$defaultMarkerExistedBefore = Test-Path -LiteralPath $defaultMarkerPath

try {
    Invoke-Tg -CliArgs @("-console", "-console-exit") -ExpectedExit 1
    $defaultMarkerExistedAfter = Test-Path -LiteralPath $defaultMarkerPath
    if ((-not $defaultMarkerExistedBefore) -and $defaultMarkerExistedAfter) {
        throw "Console without explicit -workdir touched default profile marker: $defaultMarkerPath"
    }

    $fakeDesktop = New-DisposableWorkdir -Name "fake_desktop"
    $fakeTdata = Join-Path $fakeDesktop "tdata"
    New-Item -ItemType Directory -Path $fakeTdata -Force | Out-Null
    Set-Content -Path (Join-Path $fakeTdata "D877F783D5D3EF8C") -Value "fake" -NoNewline
    Invoke-Tg -CliArgs @("-console", "-console-exit", "-workdir", $fakeDesktop) -ExpectedExit 1

    $sharedWorkdir = New-DisposableWorkdir -Name "shared_hosted"
    $first = Start-Process -FilePath $BinaryPath -ArgumentList @("-console", "-workdir", $sharedWorkdir) -PassThru
    $secondPassed = $false
    for ($attempt = 0; $attempt -lt 10; $attempt++) {
        if ((Invoke-TgExitCode -CliArgs @("-console", "-console-exit", "-workdir", $sharedWorkdir)) -eq 0) {
            $secondPassed = $true
            break
        }
    }
    if (-not $secondPassed) {
        throw "Second hosted same-workdir instance failed to complete successfully"
    }

    Invoke-Tg -CliArgs @("-quit", "-workdir", $sharedWorkdir) -ExpectedExit 0
    try { Wait-Process -Id $first.Id -Timeout 20 -ErrorAction Stop } catch {}

    $invalidLogWorkdir = New-DisposableWorkdir -Name "invalid_console_log"
    $invalidLogPath = Join-Path $invalidLogWorkdir "tdata"
    New-Item -ItemType Directory -Path $invalidLogPath -Force | Out-Null
    Invoke-Tg -CliArgs @("-console", "-console-exit", "-workdir", $invalidLogWorkdir, "-console-log", $invalidLogPath) -ExpectedExit 1

    $h1Workdir = New-DisposableWorkdir -Name "h1_one_shot"
    Invoke-Tg -CliArgs @("-console", "-console-exit", "-workdir", $h1Workdir) -ExpectedExit 0

    $h2Workdir = New-DisposableWorkdir -Name "h2_persistent"
    $h2First = Start-Process -FilePath $BinaryPath -ArgumentList @("-console", "-workdir", $h2Workdir) -PassThru
    $h2SecondPassed = $false
    for ($attempt = 0; $attempt -lt 10; $attempt++) {
        if ((Invoke-TgExitCode -CliArgs @("-console", "-console-exit", "-workdir", $h2Workdir)) -eq 0) {
            $h2SecondPassed = $true
            break
        }
    }
    if (-not $h2SecondPassed) {
        throw "H2 second instance did not exit cleanly"
    }
    Invoke-Tg -CliArgs @("-quit", "-workdir", $h2Workdir) -ExpectedExit 0
    try { Wait-Process -Id $h2First.Id -Timeout 20 -ErrorAction Stop } catch {}

    Write-Output "HOSTED_CHECKPOINT_PACKET26_REVIEW_TESTS=PASS"
}
finally {
    Get-Process -Name tg -ErrorAction SilentlyContinue | ForEach-Object {
        if ($_.Path -eq $BinaryPath) {
            try { Stop-Process -Id $_.Id -Force -ErrorAction Stop } catch {}
        }
    }
}
