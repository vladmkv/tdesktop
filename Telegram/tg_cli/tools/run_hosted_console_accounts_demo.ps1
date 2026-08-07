param(
    [string]$BinaryPath = "",
    [string]$DevProfile = "",
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

$running = @(Get-Process -Name tg -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -eq $BinaryPath
})
if ($running.Count -gt 0) {
    throw "Close the Debug Telegram window before running this demo."
}

$logPath = Join-Path $env:TEMP ("tg-accounts-demo-" + [Guid]::NewGuid().ToString("N") + ".log")
$process = Start-Process -FilePath $BinaryPath -ArgumentList @(
    "-console-accounts",
    "-workdir",
    $DevProfile,
    "-console-log",
    $logPath
) -PassThru

try {
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Account enumeration timed out after $TimeoutSeconds seconds."
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        $partial = if (Test-Path -LiteralPath $logPath) {
            (Get-Content -LiteralPath $logPath -Encoding UTF8) -join [Environment]::NewLine
        } else {
            "<no account output>"
        }
        throw "Account enumeration failed with exit code $($process.ExitCode):`n$partial"
    }
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        throw "Account enumeration produced no output log."
    }

    $lines = @(Get-Content -LiteralPath $logPath -Encoding UTF8)
    $rows = @($lines | Where-Object { $_.StartsWith("accounts-row:") })
    if ($rows.Count -eq 0) {
        throw "Account enumeration succeeded but returned no account rows."
    }

    Write-Output "Telegram CLI account demo"
    Write-Output "Profile: $DevProfile"
    Write-Output ""
    $lines | ForEach-Object { Write-Output $_ }
    Write-Output ""
    Write-Output "Demo passed: $($rows.Count) authenticated account row(s)."
} finally {
    Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
}