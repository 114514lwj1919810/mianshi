$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$uv4 = 'C:\Keil_v5\UV4\UV4.exe'
$project = Join-Path $root 'bootloader\Projects\MDK-ARM\GD_BOOT_SECURE.uvprojx'
$logDir = Join-Path $root 'bootloader\Output'

if (-not (Test-Path -LiteralPath $uv4)) {
    throw "Keil was not found: $uv4"
}
if (-not (Test-Path -LiteralPath $project)) {
    throw "Keil project was not found: $project"
}
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
foreach ($target in @('GD_BOOT_DEV', 'GD_BOOT_PROD')) {
    $log = Join-Path $logDir ("build_" + $target + ".log")
    $process = Start-Process -FilePath $uv4 -ArgumentList @(
        '-r', $project, '-t', $target, '-j0', '-o', $log
    ) -Wait -PassThru -WindowStyle Hidden
    Get-Content -LiteralPath $log -Encoding Default | Select-Object -Last 25
    if ($process.ExitCode -ne 0) {
        throw "$target build failed. See $log"
    }
}
Write-Host 'GD_BOOT_DEV and GD_BOOT_PROD were built successfully.'
