param(
    [ValidateSet('menu','build','register','package','verify','manage','factory','audio-trig','audio-back')]
    [string]$Action = 'menu'
)

$ErrorActionPreference = 'Stop'
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$env:PYTHONUTF8 = '1'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$gameProjectRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $root))
$musicRoot = Join-Path $gameProjectRoot 'music\1'

function Find-WorkflowFile([string]$name) {
    $match = Get-ChildItem -LiteralPath $root -Recurse -File -Filter $name | Select-Object -First 1
    if ($null -eq $match) { throw "Required file not found: $name" }
    return $match.FullName
}

function Get-PythonCommand {
    $command = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $command) { throw 'python.exe was not found in PATH.' }
    return $command.Source
}

function Invoke-Python([string]$scriptName, [string[]]$arguments) {
    $python = Get-PythonCommand
    $script = Find-WorkflowFile $scriptName
    # Send the child program's informational text directly to the console.
    # Without Out-Host, PowerShell also returns those lines as function output;
    # Register-Board would then return "log lines + device ID" as an array and
    # make_factory_card.py would receive the whole array as its --id value.
    & $python $script @arguments | Out-Host
    $pythonExitCode = $LASTEXITCODE
    if ($pythonExitCode -ne 0) { throw "$scriptName failed with exit code $pythonExitCode" }
}

function Read-FirmwareVariant {
    Write-Host '请选择游戏程序版本：'
    Write-Host '  1 = FUAH 带打码界面版'
    Write-Host '  2 = NO_FUAH 普通版（不带打码界面）'
    switch ((Read-Host '请输入 1 或 2').Trim()) {
        '1' { return 'FUAH' }
        '2' { return 'NO_FUAH' }
        default { throw '游戏程序版本只能输入 1 或 2。' }
    }
}

function Ensure-SigningKey { Invoke-Python 'key_management.py' @() }

function Build-Bootloader {
    Ensure-SigningKey
    $script = Find-WorkflowFile 'build_bootloader.ps1'
    & $script
}

function Build-Apps([string]$variant) {
    $script = Find-WorkflowFile 'build_apps.ps1'
    & $script -Variant $variant
}

function Build-Universal([string]$variant) {
    Ensure-SigningKey
    Build-Apps $variant
    Invoke-Python 'universal_iap_packager.py' @('--variant', $variant)
    Invoke-Python 'verify_universal_package.py' @('--variant', $variant)
}

function Register-Board {
    $deviceId = (Read-Host '请输入产品编号，例如 143-001').Trim()
    $chipUid = (Read-Host '请输入 CHIP_UID（必须是24位十六进制字符）').Trim()
    Invoke-Python 'register_device.py' @('--id', $deviceId, '--uid', $chipUid)
    return $deviceId
}

function Manage-Boards {
    Invoke-Python 'manage_devices.py' @('--list')
    Write-Host ''
    Write-Host '  1 = 启用产品'
    Write-Host '  2 = 停用产品'
    Write-Host '  0 = 返回，不修改'
    $choice = (Read-Host '请选择').Trim()
    if ($choice -eq '0') { return }
    if ($choice -ne '1' -and $choice -ne '2') { throw '只能输入 0、1 或 2。' }
    $ids = (Read-Host '请输入产品编号，多个编号用英文逗号分隔，例如 143-001,143-003').Trim()
    $status = if ($choice -eq '1') { 'active' } else { 'inactive' }
    Invoke-Python 'manage_devices.py' @('--set', $status, '--ids', $ids)
    Write-Host '提示：启用/停用状态只对修改后重新生成的升级卡生效。' -ForegroundColor Yellow
}

function Build-NewBoard {
    Write-Host '空板生产：登记新芯片 UID 和产品编号，并生成首次初始化卡。' -ForegroundColor Cyan
    $deviceId = Register-Board
    $variant = Read-FirmwareVariant
    Build-Bootloader
    Build-Universal $variant
    Invoke-Python 'make_factory_card.py' @('--id', $deviceId, '--variant', $variant)
}

function Get-NaturalSortKey([string]$text) {
    # 音频模块按写入先后确定物理序号。把文件名中的数字补齐后排序，
    # 可保证 1、2、3、...、10 的自然顺序，而不是 1、10、2 的文本顺序。
    return [regex]::Replace(
        $text,
        '\d+',
        { param($match) $match.Value.PadLeft(20, '0') }
    ).ToLowerInvariant()
}

function Copy-AudioDirectorySequential(
    [string]$sourceDirectory,
    [string]$destinationDirectory,
    [ref]$copiedCount,
    [int]$totalCount
) {
    if (-not (Test-Path -LiteralPath $destinationDirectory)) {
        [void](New-Item -ItemType Directory -Path $destinationDirectory)
    }

    # 先顺序写当前目录中的文件，再顺序建立并写入每个子目录。
    # ADVERT1 等文件夹的内容也会从第一个文件一直写到最后一个文件，数量不固定。
    $files = @(Get-ChildItem -LiteralPath $sourceDirectory -File |
        Sort-Object @{ Expression = { Get-NaturalSortKey $_.Name } }, @{ Expression = { $_.Name } })
    foreach ($file in $files) {
        $targetFile = Join-Path $destinationDirectory $file.Name
        Copy-Item -LiteralPath $file.FullName -Destination $targetFile -Force
        if ((Get-Item -LiteralPath $targetFile).Length -ne $file.Length) {
            throw "复制校验失败：$($file.FullName)"
        }
        $copiedCount.Value++
        $percent = if ($totalCount -gt 0) {
            [int](100 * $copiedCount.Value / $totalCount)
        } else { 100 }
        Write-Progress -Activity '正在按顺序写入音频' -Status "$($copiedCount.Value)/$totalCount  $($file.Name)" -PercentComplete $percent
        Write-Host ('  [{0}/{1}] {2}' -f $copiedCount.Value, $totalCount, $file.FullName.Substring($musicRoot.Length + 1))
    }

    $directories = @(Get-ChildItem -LiteralPath $sourceDirectory -Directory |
        Sort-Object @{ Expression = { Get-NaturalSortKey $_.Name } }, @{ Expression = { $_.Name } })
    foreach ($directory in $directories) {
        $targetSubDirectory = Join-Path $destinationDirectory $directory.Name
        Copy-AudioDirectorySequential $directory.FullName $targetSubDirectory $copiedCount $totalCount
    }
}

function Read-AudioTargetDrive {
    $entered = (Read-Host '请输入音频芯片盘符（直接回车默认 F）').Trim()
    if ([string]::IsNullOrWhiteSpace($entered)) { $entered = 'F' }
    $driveLetter = ($entered -replace '[:\\/\s]', '').ToUpperInvariant()
    if ($driveLetter -notmatch '^[A-Z]$') {
        throw '盘符格式错误，请只输入一个字母，例如 F。'
    }

    $systemDriveLetter = ([System.IO.Path]::GetPathRoot($env:SystemRoot)).Substring(0, 1).ToUpperInvariant()
    if ($driveLetter -eq $systemDriveLetter) {
        throw '禁止把 Windows 系统盘作为音频目标盘。'
    }

    $volume = Get-Volume -DriveLetter $driveLetter -ErrorAction SilentlyContinue
    if ($null -eq $volume) {
        throw "找不到 $driveLetter`: 盘。请插入音频芯片并确认 Windows 已分配盘符。"
    }
    return [pscustomobject]@{
        Letter = $driveLetter
        Root = "$driveLetter`:\"
        Volume = $volume
    }
}

function Prepare-AudioTarget([object]$target, [string]$label) {
    $volume = $target.Volume
    $fileSystem = [string]$volume.FileSystemType
    if ([string]::IsNullOrWhiteSpace($fileSystem) -or $fileSystem -eq 'Unknown') {
        $fileSystem = [string]$volume.FileSystem
    }
    if ([string]::IsNullOrWhiteSpace($fileSystem)) { $fileSystem = 'RAW/未格式化' }

    $sizeText = if ($null -ne $volume.Size -and [uint64]$volume.Size -gt 0) {
        '{0:N1} MB' -f ([double]$volume.Size / 1MB)
    } else { '未知' }

    Write-Host ''
    Write-Host '即将清空并顺序写入以下目标盘：' -ForegroundColor Yellow
    Write-Host "  盘符：$($target.Letter):"
    Write-Host "  卷标：$($volume.FileSystemLabel)"
    Write-Host "  容量：$sizeText"
    Write-Host "  文件系统：$fileSystem"
    Write-Host '目标盘中原有的全部文件都会被删除，且无法恢复。' -ForegroundColor Red
    $confirm = (Read-Host "确认无误请输入目标盘符 $($target.Letter)，其他输入取消").Trim().ToUpperInvariant()
    if ($confirm -ne $target.Letter) { throw '用户取消了音频写入。' }

    $compatible = $fileSystem -match '^(FAT|FAT32)$'
    if (-not $compatible) {
        $formatType = if ($null -ne $volume.Size -and [uint64]$volume.Size -gt 2GB) { 'FAT32' } else { 'FAT' }
        Write-Host "目标盘需要格式化，正在快速格式化为 $formatType……" -ForegroundColor Yellow
        Format-Volume -DriveLetter $target.Letter -FileSystem $formatType -NewFileSystemLabel $label -Force -Confirm:$false | Out-Null
        Start-Sleep -Seconds 2
    } else {
        Write-Host "目标盘已经是 $fileSystem，不重复格式化。"
    }

    # 即使无需格式化也必须清空后重写，否则覆盖文件不能保证物理写入顺序，
    # 同时还可能残留已经从源目录删除的旧音频。
    $resolvedTarget = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $target.Root -ErrorAction Stop).Path).TrimEnd('\')
    $expectedTarget = [System.IO.Path]::GetFullPath($target.Root).TrimEnd('\')
    if ($resolvedTarget -ne $expectedTarget) {
        throw "目标盘路径校验失败：$resolvedTarget"
    }
    Get-ChildItem -LiteralPath $target.Root -Force | Remove-Item -Recurse -Force
}

function Write-AudioChip([ValidateSet('trig','back')][string]$audioType) {
    $sourceDirectory = Join-Path $musicRoot $audioType
    if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) {
        throw "找不到音频源目录：$sourceDirectory"
    }

    $description = if ($audioType -eq 'trig') { '触发音效芯片（trig）' } else { '背景音乐芯片（back）' }
    $label = if ($audioType -eq 'trig') { 'JOKER_TRIG' } else { 'JOKER_BACK' }
    $sourceFiles = @(Get-ChildItem -LiteralPath $sourceDirectory -File -Recurse)
    if ($sourceFiles.Count -eq 0) { throw "音频源目录为空：$sourceDirectory" }

    Write-Host ''
    Write-Host "准备制作：$description" -ForegroundColor Cyan
    Write-Host "源目录：$sourceDirectory"
    Write-Host "文件总数：$($sourceFiles.Count)（自动读取，不限制固定数量）"
    $target = Read-AudioTargetDrive
    Prepare-AudioTarget $target $label

    $copiedCount = 0
    Copy-AudioDirectorySequential $sourceDirectory $target.Root ([ref]$copiedCount) $sourceFiles.Count
    Write-Progress -Activity '正在按顺序写入音频' -Completed

    $targetFiles = @(Get-ChildItem -LiteralPath $target.Root -File -Recurse)
    if ($targetFiles.Count -ne $sourceFiles.Count) {
        throw "文件数量校验失败：源目录 $($sourceFiles.Count) 个，目标盘 $($targetFiles.Count) 个。"
    }
    Write-Host "$description 写入完成，共 $copiedCount 个文件。请使用 Windows“安全删除硬件”后再拔出。" -ForegroundColor Green
}

function Show-Menu {
    Write-Host ''
    Write-Host '元气小丑 GD32 生产、升级与音频制作工具' -ForegroundColor Cyan
    Write-Host '  1 = 烧录/初始化一块全新空板'
    Write-Host '  2 = 为全部已启用产品生成一张通用 TF 升级卡'
    Write-Host '  3 = 启用或停用产品表中的机器'
    Write-Host '  4 = 查看产品表（不显示密钥）'
    Write-Host '  5 = 验证最新通用升级包'
    Write-Host '  6 = 编译 DEV 和 PROD Bootloader'
    Write-Host '  7 = 仅登记一块机器'
    Write-Host '  8 = 顺序制作触发音效芯片（music\1\trig）'
    Write-Host '  9 = 顺序制作背景音乐芯片（music\1\back）'
    Write-Host '  0 = 退出'
    return (Read-Host '请选择').Trim()
}

try {
    if ($Action -eq 'menu') {
        switch (Show-Menu) {
            '1' { Build-NewBoard }
            '2' { $variant = Read-FirmwareVariant; Build-Universal $variant }
            '3' { Manage-Boards }
            '4' { Invoke-Python 'manage_devices.py' @('--list') }
            '5' { $variant = Read-FirmwareVariant; Invoke-Python 'verify_universal_package.py' @('--variant', $variant) }
            '6' { Build-Bootloader }
            '7' { [void](Register-Board) }
            '8' { Write-AudioChip 'trig' }
            '9' { Write-AudioChip 'back' }
            '0' { exit 0 }
            default { throw '菜单选项不存在。' }
        }
    } else {
        switch ($Action) {
            'build' { Build-Bootloader }
            'register' { [void](Register-Board) }
            'package' { $variant = Read-FirmwareVariant; Build-Universal $variant }
            'verify' { $variant = Read-FirmwareVariant; Invoke-Python 'verify_universal_package.py' @('--variant', $variant) }
            'manage' { Manage-Boards }
            'factory' { Build-NewBoard }
            'audio-trig' { Write-AudioChip 'trig' }
            'audio-back' { Write-AudioChip 'back' }
        }
    }
    Write-Host '操作成功。' -ForegroundColor Green
    exit 0
}
catch {
    Write-Host ('操作失败：' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
