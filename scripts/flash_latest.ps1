# BikePower C — Windows 烧录脚本
# 一键编译并烧录当前工作区最新版 BikePower C 固件到 ESP32-C3 开发板
#
# 用法:
#   .\scripts\flash_latest.ps1                        # 自动识别串口并烧录
#   .\scripts\flash_latest.ps1 -Port COM3             # 指定串口
#   .\scripts\flash_latest.ps1 -Board m5stamp         # 指定板型
#   .\scripts\flash_latest.ps1 -NoChecks              # 跳过快速静态门禁
#   .\scripts\flash_latest.ps1 -NoBuild               # 跳过编译，直接烧录
#   .\scripts\flash_latest.ps1 -Erase                 # 烧录前擦除整片 flash
#   .\scripts\flash_latest.ps1 -Monitor               # 烧录后进入串口监视器
#   .\scripts\flash_latest.ps1 -NoCapture             # 烧录后不抓取启动日志
#   .\scripts\flash_latest.ps1 -CaptureSeconds 30     # 抓取 30 秒启动日志
#   .\scripts\flash_latest.ps1 -IdfPath "C:\Espressif\frameworks\esp-idf-v5.5.4"
#
# 首次从旧分区切到当前 4MB factory 布局时使用 -Erase。

[CmdletBinding()]
param(
    [string]$Port = "",
    [ValidateSet("generic", "m5stamp")]
    [string]$Board = "generic",
    [switch]$NoChecks,
    [switch]$NoBuild,
    [switch]$Erase,
    [switch]$Monitor,
    [switch]$NoCapture,
    [int]$CaptureSeconds = 15,
    [string]$IdfPath = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $ScriptDir "idf_common.psm1") -Force
$ProjectDir = $Global:BikePowerProjectDir
$FlashDefaults = "sdkconfig.flash.tmp"
$FlashBoardFile = "sdkconfig.flash.board"
$SdkconfigBackup = $null

function Run-QuickChecks {
    Write-Log "运行快速静态门禁"
    $checks = @(
        "tests\check_portal_js.py",
        "tests\check_config_consistency.py",
        "tests\check_tasks_wdt.py",
        "tests\check_nvs_schema.py",
        "tests\check_board_desc.py"
    )
    foreach ($check in $checks) {
        $fullPath = Join-Path $ProjectDir $check
        if (Test-Path $fullPath) {
            try {
                Invoke-PythonScript -ScriptPath $fullPath
            } catch {
                Die "静态门禁失败: $check"
            }
        }
    }
    Write-Success "快速静态门禁全部通过"
}

function Prepare-BoardDefaults {
    $kconfig = switch ($Board) {
        "generic" { "CONFIG_BOARD_GENERIC_C3=y" }
        "m5stamp" { "CONFIG_BOARD_M5STAMP_C3=y" }
        default { Die "未知板型: $Board" }
    }

    $sdkconfigPath = Join-Path $ProjectDir "sdkconfig"
    if (Test-Path $sdkconfigPath -and -not $SdkconfigBackup) {
        $script:SdkconfigBackup = Join-Path $ProjectDir "sdkconfig.flash.backup"
        Copy-Item $sdkconfigPath $script:SdkconfigBackup -Force
    }

    $boardFile = Join-Path $ProjectDir $FlashBoardFile
    $defaultsFile = Join-Path $ProjectDir "sdkconfig.defaults"
    $mergedFile = Join-Path $ProjectDir $FlashDefaults
    Set-Content -Path $boardFile -Value $kconfig -Encoding ASCII
    Get-Content $defaultsFile, $boardFile | Set-Content -Path $mergedFile -Encoding ASCII
    Remove-Item $boardFile -Force
    if (Test-Path $sdkconfigPath) { Remove-Item $sdkconfigPath -Force }
}

function Cleanup-FlashConfig {
    $flashDefaults = Join-Path $ProjectDir $FlashDefaults
    $flashBoard = Join-Path $ProjectDir $FlashBoardFile
    if (Test-Path $flashDefaults) { Remove-Item $flashDefaults -Force }
    if (Test-Path $flashBoard) { Remove-Item $flashBoard -Force }
    if ($SdkconfigBackup -and (Test-Path $SdkconfigBackup)) {
        Move-Item $SdkconfigBackup (Join-Path $ProjectDir "sdkconfig") -Force
    }
}

try {
    if ($CaptureSeconds -lt 0) {
        Die "CaptureSeconds 必须是非负整数"
    }

    if ($Board -notin @("generic", "m5stamp")) {
        Die "Board 仅支持 generic 或 m5stamp"
    }

    $IdfPath = Find-EspIdf -UserSpecified $IdfPath
    if ($null -eq $IdfPath) {
        Die "找不到 ESP-IDF，请通过 -IdfPath 指定，或设置 IDF_PATH 环境变量。常见位置: C:\Espressif\frameworks\esp-idf-v5.5.4"
    }

    Activate-IdfEnv -IdfPath $IdfPath

    if (-not $Port) {
        $Port = Detect-SerialPort
        if (-not $Port) {
            Die "找不到串口设备，请用 -Port 指定"
        }
    }

    if (-not (Test-SerialPort -PortName $Port)) {
        Die "串口 $Port 不存在，请检查设备连接或用 -Port 指定"
    }

    Write-Log "使用串口: $Port"

    if (-not $NoChecks) {
        Run-QuickChecks
    } else {
        Write-Host "  跳过快速静态门禁 (-NoChecks)" -ForegroundColor Yellow
    }

    if (-not $NoBuild) {
        Write-Log "编译固件（board=$Board）"
        Prepare-BoardDefaults
        Clear-StaleBoardCache
        Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @(
            "-D", "SDKCONFIG_DEFAULTS=$FlashDefaults",
            "reconfigure"
        )
        Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @(
            "-D", "SDKCONFIG_DEFAULTS=$FlashDefaults",
            "build"
        )
    } else {
        Write-Host "  跳过编译 (-NoBuild)" -ForegroundColor Yellow
    }

    if ($Erase) {
        Write-Log "擦除整片 flash"
        Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @("-p", $Port, "erase-flash")
    }

    Write-Log "烧录固件到 $Port"
    Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @("-p", $Port, "flash")

    if (-not $NoCapture -and $CaptureSeconds -gt 0) {
        $logsDir = Join-Path $ProjectDir "tests\logs"
        if (-not (Test-Path $logsDir)) {
            New-Item -Path $logsDir -ItemType Directory -Force | Out-Null
        }
        $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
        $logFile = Join-Path $logsDir "flash_latest_$timestamp.log"
        Write-Log "抓取启动日志 ${CaptureSeconds}s: $logFile"

        $captureScript = Join-Path $ProjectDir "tests\serial_capture.py"
        if (Test-Path $captureScript) {
            $pythonExe = Get-IdfPython
            & $pythonExe $captureScript --port $Port --duration $CaptureSeconds 2>&1 | Tee-Object -FilePath $logFile
        } else {
            Write-Host "  警告: 找不到 serial_capture.py，跳过启动日志抓取" -ForegroundColor Yellow
        }
    }

    if ($Monitor) {
        Write-Log "进入串口监视器"
        Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @("-p", $Port, "monitor")
    }

    Write-Host ""
    Write-Success "烧录完成"
} finally {
    Cleanup-FlashConfig
}
