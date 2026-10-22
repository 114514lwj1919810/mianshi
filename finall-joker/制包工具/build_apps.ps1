param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('FUAH', 'NO_FUAH')]
    [string]$Variant
)

$ErrorActionPreference = 'Stop'
$finalRoot = Split-Path -Parent $PSScriptRoot
$jokerRoot = Split-Path -Parent $finalRoot
$appRoot = Join-Path $jokerRoot 'joker-v1'
$uv4 = 'C:\Keil_v5\UV4\UV4.exe'

if ($Variant -eq 'FUAH') {
    $project = Join-Path $appRoot 'Projects\MDK-ARM\joker_gd32f407_iap.uvprojx'
    $targets = @('Joker_V1_APP_A', 'Joker_V1_APP_B')
    $outputs = @(
        (Join-Path $appRoot 'Output\IAP_A\IAP_A.bin'),
        (Join-Path $appRoot 'Output\IAP_B\IAP_B.bin')
    )
}
else {
    $project = Join-Path $appRoot 'Projects\MDK-ARM\joker_gd32f407_no_lock_iap.uvprojx'
    $targets = @('Joker_V1_NO_LOCK_APP_A', 'Joker_V1_NO_LOCK_APP_B')
    $outputs = @(
        (Join-Path $appRoot 'Output\NO_LOCK_IAP_A\NO_LOCK_IAP_A.bin'),
        (Join-Path $appRoot 'Output\NO_LOCK_IAP_B\NO_LOCK_IAP_B.bin')
    )
}

if (-not (Test-Path -LiteralPath $uv4)) { throw "Keil was not found: $uv4" }
if (-not (Test-Path -LiteralPath $project)) { throw "App project was not found: $project" }
$logDir = Join-Path $appRoot 'Output'
for ($i = 0; $i -lt 2; $i++) {
    $log = Join-Path $logDir ("secure_package_build_" + $targets[$i] + ".log")
    $buildStartedUtc = (Get-Date).ToUniversalTime()
    $process = Start-Process -FilePath $uv4 -ArgumentList @(
        '-r', $project, '-t', $targets[$i], '-j0', '-o', $log
    ) -Wait -PassThru -WindowStyle Hidden
    Get-Content -LiteralPath $log -Encoding Default | Select-Object -Last 20
    if ($process.ExitCode -ne 0) { throw "$($targets[$i]) build failed. See $log" }
    if (-not (Test-Path -LiteralPath $outputs[$i])) { throw "Output was not generated: $($outputs[$i])" }
    if ((Get-Item -LiteralPath $outputs[$i]).LastWriteTimeUtc -lt $buildStartedUtc) {
        throw "$($targets[$i]) output is stale: $($outputs[$i])"
    }
}
Write-Host "$Variant App A/B were built successfully."
