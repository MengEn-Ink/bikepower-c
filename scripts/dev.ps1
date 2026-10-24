# BikePower C - Windows 统一开发入口
# 对应 scripts/platform/unix/build_firmware.sh，提供 build/flash/monitor/test/check/clean 等子命令。
#
# 用法:
#   .\scripts\dev.ps1 build              # 编译固件（增量）
#   .\scripts\dev.ps1 build -Board m5stamp   # 指定板型编译
#   .\scripts\dev.ps1 flash              # 自动探测串口并烧录
#   .\scripts\dev.ps1 flash -Port COM3   # 指定串口烧录
#   .\scripts\dev.ps1 flash -NoBuild     # 跳过编译直接烧录
#   .\scripts\dev.ps1 monitor            # 串口监视器
#   .\scripts\dev.ps1 monitor -Port COM3
#   .\scripts\dev.ps1 test               # 运行 host 单测
#   .\scripts\dev.ps1 check              # 运行静态门禁
#   .\scripts\dev.ps1 clean              # 清理构建目录
#   .\scripts\dev.ps1 reconfigure        # 重新生成 CMake 配置
#   .\scripts\dev.ps1 erase              # 擦除整片 flash
#   .\scripts\dev.ps1 -IdfPath "C:\Espressif\frameworks\esp-idf-v5.5.4" build
#
# 设计原则:
#   - 不硬编码 ESP-IDF 路径，自动探测（见 idf_common.psm1）
#   - 不硬编码串口，自动探测
#   - 异常情况给出明确提示

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("build", "flash", "monitor", "test", "check", "clean", "reconfigure", "erase", "stats", "help", "")]
    [string]$Action = "help",

    [ValidateSet("generic", "m5stamp")]
    [string]$Board = "generic",

    [string]$Port = "",

    [switch]$NoBuild,
    [switch]$NoChecks,
    [switch]$Erase,
    [switch]$SkipHostTest,

    [string]$IdfPath = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# 导入共享模块
Import-Module (Join-Path $ScriptDir "idf_common.psm1") -Force

$ProjectDir = $Global:BikePowerProjectDir

# 显示帮助
function Show-Help {
    Write-Host ""
    Write-Host "BikePower C - Windows 统一开发入口" -ForegroundColor Magenta
    Write-Host ""
    Write-Host "用法: .\scripts\dev.ps1 [action] [options]"
    Write-Host ""
    Write-Host "Actions:"
    Write-Host "  build        编译固件（增量编译）"
    Write-Host "  flash        编译并烧录固件"
    Write-Host "  monitor      串口监视器"
    Write-Host "  test         运行 host 单测"
    Write-Host "  check        运行静态门禁检查"
    Write-Host "  clean        清理构建目录（idf.py fullclean）"
    Write-Host "  reconfigure  重新生成 CMake 配置"
    Write-Host "  erase        擦除整片 flash"
    Write-Host "  stats        显示 ccache 缓存统计"
    Write-Host "  help         显示此帮助"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -Board [generic|m5stamp]   指定板型（默认 generic）"
    Write-Host "  -Port COMx                 指定串口（默认自动探测）"
    Write-Host "  -NoBuild                   跳过编译（仅 flash 时有效）"
    Write-Host "  -NoChecks                  跳过静态门禁"
    Write-Host "  -SkipHostTest              跳过 host 单测"
    Write-Host "  -IdfPath [path]            指定 ESP-IDF 安装路径"
    Write-Host ""
    Write-Host "示例:"
    Write-Host "  .\scripts\dev.ps1 build"
    Write-Host "  .\scripts\dev.ps1 build -Board m5stamp"
    Write-Host "  .\scripts\dev.ps1 flash -Port COM3"
    Write-Host "  .\scripts\dev.ps1 flash -NoBuild -Port COM3"
    Write-Host "  .\scripts\dev.ps1 monitor -Port COM3"
    Write-Host "  .\scripts\dev.ps1 check"
    Write-Host ""
}

# 准备板型临时配置
function Prepare-BoardConfig {
    $kconfig = switch ($Board) {
        "generic" { "CONFIG_BOARD_GENERIC_C3=y" }
        "m5stamp" { "CONFIG_BOARD_M5STAMP_C3=y" }
    }

    $boardFile = Join-Path $ProjectDir "sdkconfig.dev.board.tmp"
    $defaultsFile = Join-Path $ProjectDir "sdkconfig.defaults"
    $mergedFile = Join-Path $ProjectDir "sdkconfig.dev.merged.tmp"

    # 备份现有 sdkconfig
    $sdkconfigPath = Join-Path $ProjectDir "sdkconfig"
    $backupPath = Join-Path $ProjectDir "sdkconfig.dev.backup"
    if (Test-Path $sdkconfigPath) {
        Copy-Item $sdkconfigPath $backupPath -Force
    }

    Set-Content -Path $boardFile -Value $kconfig -Encoding ASCII
    Get-Content $defaultsFile, $boardFile | Set-Content -Path $mergedFile -Encoding ASCII
    Remove-Item $boardFile -Force

    return $mergedFile
}

# 清理板型临时配置
function Cleanup-BoardConfig {
    $mergedFile = Join-Path $ProjectDir "sdkconfig.dev.merged.tmp"
    $backupPath = Join-Path $ProjectDir "sdkconfig.dev.backup"

    if (Test-Path $mergedFile) { Remove-Item $mergedFile -Force }
    if (Test-Path $backupPath) {
        Move-Item $backupPath (Join-Path $ProjectDir "sdkconfig") -Force
    }
}

# 编译固件
function Do-Build {
    Write-Log "编译固件（board=$Board）"

    # 显示 ccache 状态
    $ccacheExe = Get-Command "ccache" -ErrorAction SilentlyContinue
    if ($ccacheExe) {
        Write-Host "  ccache: 已启用" -ForegroundColor Gray
    } else {
        Write-Host "  ccache: 未启用（安装后可加速增量编译）" -ForegroundColor Gray
    }

    $buildStartTime = Get-Date

    Clear-StaleBoardCache

    $mergedFile = Prepare-BoardConfig
    try {
        Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @(
            "-D", "SDKCONFIG_DEFAULTS=$mergedFile",
            "reconfigure"
        )
        Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @(
            "-D", "SDKCONFIG_DEFAULTS=$mergedFile",
            "build"
        )
    } finally {
        Cleanup-BoardConfig
    }

    $buildDuration = (Get-Date) - $buildStartTime
    Write-Success ("编译完成，耗时 {0:hh\:mm\:ss}" -f $buildDuration)
    # 显示 ccache 命中率摘要
    $ccacheExe = Get-Command "ccache" -ErrorAction SilentlyContinue
    if ($ccacheExe) {
        $hitLine = & $ccacheExe.Source --show-stats 2>$null | Select-String "Hits:" | Select-Object -First 1
        if ($hitLine) {
            Write-Host ("  ccache: " + $hitLine.Line.Trim()) -ForegroundColor Gray
        }
    }
}

# 烧录固件
function Do-Flash {
    if (-not $NoBuild) {
        Do-Build
    } else {
        Write-Host "  跳过编译 (-NoBuild)" -ForegroundColor Yellow
    }

    if (-not $Port) {
        $Port = Detect-SerialPort
        if (-not $Port) {
            Die "找不到串口设备，请用 -Port 指定"
        }
    }
    Write-Log "使用串口: $Port"

    if (-not (Test-SerialPort $Port)) {
        Die "串口不存在: $Port"
    }

    if ($Erase) {
        Write-Log "擦除整片 flash"
        Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @("-p", $Port, "erase-flash")
    }

    Write-Log "烧录固件到 $Port"
    Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @("-p", $Port, "flash")

    Write-Success "烧录完成"
}

# 串口监视器
function Do-Monitor {
    if (-not $Port) {
        $Port = Detect-SerialPort
        if (-not $Port) {
            Die "找不到串口设备，请用 -Port 指定"
        }
    }
    Write-Log "串口监视器: $Port"
    Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @("-p", $Port, "monitor")
}

# 运行 host 单测
function Do-Test {
    Write-Log "运行 host 单测"

    $makeCmd = Get-Command "make" -ErrorAction SilentlyContinue
    $gccCmd = Get-Command "gcc" -ErrorAction SilentlyContinue
    $clangCmd = Get-Command "clang" -ErrorAction SilentlyContinue
    $mingwMake = Get-Command "mingw32-make" -ErrorAction SilentlyContinue

    $actualMake = $makeCmd
    if (-not $actualMake -and $mingwMake) { $actualMake = $mingwMake }

    if (-not $actualMake -or (-not $gccCmd -and -not $clangCmd)) {
        Write-Warn "未找到 make/gcc/clang 编译工具链，跳过 host 单测"
        Write-Host "  提示: 安装 MSYS2 或 MinGW 后可运行 host 单测" -ForegroundColor Gray
        return
    }

    $testDir = Join-Path $ProjectDir "test"
    Push-Location $testDir
    try {
        if ($clangCmd) {
            Invoke-SafeCommand -Command $actualMake.Source -ArgsList @("CC=clang") -WorkingDir $testDir
        } else {
            Invoke-SafeCommand -Command $actualMake.Source -ArgsList @() -WorkingDir $testDir
        }
        $testBin = Join-Path $testDir "test_domain.exe"
        if (-not (Test-Path $testBin)) {
            Die "host 单测编译产物不存在: $testBin"
        }
        & $testBin
        if ($LASTEXITCODE -ne 0) {
            Die "host 单测失败 (exit=$LASTEXITCODE)"
        }
    } finally {
        Pop-Location
    }
    Write-Success "host 单测通过"
}

# 运行静态门禁
function Do-Check {
    Write-Log "运行静态门禁检查"
    $checks = @(
        "tests\check_config_consistency.py",
        "tests\check_portal_js.py",
        "tests\check_portal_structure.py",
        "tests\check_nvs_schema.py",
        "tests\check_tasks_wdt.py",
        "tests\check_board_desc.py",
        "tests\check_ble_advertising_fields.py"
    )
    foreach ($check in $checks) {
        $fullPath = Join-Path $ProjectDir $check
        if (Test-Path $fullPath) {
            Write-Host "  运行: $check" -ForegroundColor Gray
            Invoke-PythonScript -ScriptPath $fullPath
        }
    }
    Write-Success "静态门禁全部通过"
}

# 清理构建
function Do-Clean {
    Write-Log "清理构建目录（idf.py fullclean）"
    Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @("fullclean")
    Write-Success "清理完成"
}

# 重新配置
function Do-Reconfigure {
    Write-Log "重新生成 CMake 配置"
    Clear-StaleBoardCache
    Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @("reconfigure")
    Write-Success "重新配置完成"
}

# 擦除 flash
function Do-Erase {
    if (-not $Port) {
        $Port = Detect-SerialPort
        if (-not $Port) {
            Die "找不到串口设备，请用 -Port 指定"
        }
    }
    Write-Log "擦除整片 flash: $Port"
    Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @("-p", $Port, "erase-flash")
    Write-Success "擦除完成"
}

# === 主流程 ===

if ($Action -eq "help" -or -not $Action) {
    Show-Help
    exit 0
}

Write-Host ""
Write-Host "=== BikePower C 开发工具（Windows） ===" -ForegroundColor Magenta
Write-Host ("项目目录: " + $ProjectDir)
Write-Host ("动作: " + $Action)
if ($Board -ne "generic") { Write-Host ("板型: " + $Board) }
if ($Port) { Write-Host ("串口: " + $Port) }
Write-Host ""

# check 和 test 只需要 Python，不需要 ESP-IDF
if ($Action -eq "check") {
    Do-Check
    exit 0
}

if ($Action -eq "test") {
    Do-Test
    exit 0
}

# stats 只需要 ccache，不需要 ESP-IDF
if ($Action -eq "stats") {
    $ccacheDir = Join-Path $ProjectDir ".ccache"
    if (-not (Test-Path $ccacheDir)) {
        Die "ccache 缓存目录不存在: $ccacheDir`n  请先执行一次编译。"
    }
    $env:CCACHE_DIR = $ccacheDir
    Show-CcacheStats
    exit 0
}

# build/flash/monitor/clean/reconfigure/erase 需要 ESP-IDF 环境
$IdfPath = Find-EspIdf -UserSpecified $IdfPath
if ($null -eq $IdfPath) {
    Die "找不到 ESP-IDF 环境`n  请通过 -IdfPath 指定 ESP-IDF 安装目录，或设置 IDF_PATH 环境变量。`n  常见安装位置: C:\Espressif\frameworks\ 或 %USERPROFILE%\esp\"
}

Activate-IdfEnv -IdfPath $IdfPath

switch ($Action) {
    "build" {
        if (-not $NoChecks) { Do-Check }
        Do-Build
    }
    "flash" {
        if (-not $NoChecks) { Do-Check }
        Do-Flash
    }
    "monitor" { Do-Monitor }
    "clean" { Do-Clean }
    "reconfigure" { Do-Reconfigure }
    "erase" { Do-Erase }
    default {
        Show-Help
        exit 1
    }
}
