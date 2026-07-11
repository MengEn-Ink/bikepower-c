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
$ProjectDir = Resolve-Path (Join-Path $ScriptDir "..")
$FlashDefaults = "sdkconfig.flash.tmp"
$FlashBoardFile = "sdkconfig.flash.board"
$SdkconfigBackup = $null

function Write-Log {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "[OK] $Message" -ForegroundColor Green
}

function Write-Fail {
    param([string]$Message)
    Write-Host "[FAIL] $Message" -ForegroundColor Red
}

function Die {
    param([string]$Message)
    Write-Fail $Message
    exit 1
}

function Invoke-SafeCommand {
    param(
        [string]$Command,
        [string[]]$ArgsList = @(),
        [string]$WorkingDir = $ProjectDir,
        [switch]$IgnoreExitCode
    )
    Write-Host "+ $Command $($ArgsList -join ' ')" -ForegroundColor Gray
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Command
    $psi.Arguments = ($ArgsList -join ' ')
    $psi.WorkingDirectory = $WorkingDir
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardInput = $true
    $proc = [System.Diagnostics.Process]::Start($psi)
    $output = $proc.StandardOutput.ReadToEnd()
    $error = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()
    if ($output) { Write-Host $output }
    if ($error -and -not $IgnoreExitCode) { Write-Host $error -ForegroundColor Yellow }
    if ($proc.ExitCode -ne 0 -and -not $IgnoreExitCode) {
        throw "命令失败 (exit=$($proc.ExitCode)): $Command $($ArgsList -join ' ')"
    }
    return $proc.ExitCode
}

function Find-EspIdf {
    param([string]$UserSpecified = "")
    if ($UserSpecified -and (Test-Path (Join-Path $UserSpecified "export.ps1"))) {
        return (Resolve-Path $UserSpecified).Path
    }
    if ($env:IDF_PATH -and (Test-Path (Join-Path $env:IDF_PATH "export.ps1"))) {
        return $env:IDF_PATH
    }
    $candidates = @(
        "C:\Espressif\frameworks\esp-idf-v5.5.4",
        "C:\Espressif\frameworks\esp-idf-v5.5.1",
        "C:\Espressif\frameworks\esp-idf-v5.5",
        "$env:USERPROFILE\esp\esp-idf-v5.5.4",
        "$env:USERPROFILE\esp\esp-idf-v5.5.1",
        "$env:USERPROFILE\esp\esp-idf"
    )
    foreach ($cand in $candidates) {
        if ($cand -and (Test-Path (Join-Path $cand "export.ps1"))) {
            return $cand
        }
    }
    if (Get-Command "idf.py" -ErrorAction SilentlyContinue) {
        return ""
    }
    return $null
}

function Activate-IdfEnv {
    param([string]$IdfPath)
    if (-not $IdfPath) {
        Write-Log "使用当前 shell 中已激活的 ESP-IDF"
        return
    }
    Write-Log "激活 ESP-IDF: $IdfPath"
    $exportScript = Join-Path $IdfPath "export.ps1"
    if (-not (Test-Path $exportScript)) {
        Die "找不到 ESP-IDF 导出脚本: $exportScript"
    }
    . $exportScript
    Write-Success "ESP-IDF 已激活: IDF_PATH=$env:IDF_PATH"
}

function Get-IdfPython {
    $pyEnv = $env:IDF_PYTHON_ENV_PATH
    if ($pyEnv -and (Test-Path (Join-Path $pyEnv "Scripts\python.exe"))) {
        return Join-Path $pyEnv "Scripts\python.exe"
    }
    $candidates = @(
        ($env:USERPROFILE + "\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"),
        ($env:USERPROFILE + "\.espressif\python_env\idf5.5_py3.9_env\Scripts\python.exe")
    )
    foreach ($cand in $candidates) {
        if (Test-Path $cand) { return $cand }
    }
    return "python"
}

function Invoke-IdfCommand {
    param(
        [string]$IdfPath,
        [string[]]$IdfArgs = @(),
        [string]$WorkingDir = $ProjectDir
    )
    $pythonExe = Get-IdfPython
    if ($IdfPath) {
        $idfPy = Join-Path $IdfPath "tools\idf.py"
        if (-not (Test-Path $idfPy)) {
            Die "找不到 idf.py: $idfPy"
        }
    } else {
        $idfPy = "idf.py"
    }
    $allArgs = @($idfPy) + $IdfArgs
    return Invoke-SafeCommand -Command $pythonExe -ArgsList $allArgs -WorkingDir $WorkingDir
}

function Detect-Port {
    $detected = ""
    try {
        $pyscript = @'
try:
    from serial.tools import list_ports
except Exception:
    import sys
    sys.exit(0)

ports = list(list_ports.comports())

def score(port):
    hwid = (port.hwid or "").upper()
    desc = (port.description or "").lower()
    device = port.device or ""
    if "303A:1001" in hwid:
        return (0, device)
    if "ESP" in hwid or "espressif" in desc or "jtag/serial" in desc or "usb serial" in desc:
        return (1, device)
    if device.upper().startswith("COM"):
        try:
            num = int(device[3:])
            return (2, device)
        except:
            pass
    return (9, device)

candidates = [p for p in ports if score(p)[0] < 9]
if candidates:
    print(sorted(candidates, key=score)[0].device)
'@
        $detected = (& python -c $pyscript 2>$null)
    } catch {}

    if ($detected) {
        return $detected
    }

    try {
        $comPorts = [System.IO.Ports.SerialPort]::GetPortNames()
        if ($comPorts -and $comPorts.Count -gt 0) {
            return $comPorts[0]
        }
    } catch {}

    return $null
}

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
            & python $fullPath
            if ($LASTEXITCODE -ne 0) {
                Die "静态门禁失败: $check"
            }
        }
    }
    Write-Success "快速静态门禁全部通过"
}

function Clear-StaleBoardCache {
    $cmakeCache = Join-Path (Join-Path $ProjectDir "build") "CMakeCache.txt"
    if (-not (Test-Path $cmakeCache)) { return }
    $content = Get-Content $cmakeCache -Raw
    if ($content -match "sdkconfig\.board\.tmp|sdkconfig\.flash\.tmp") {
        Write-Host "  清理 build/CMakeCache.txt 中残留的临时板型配置" -ForegroundColor Yellow
        Remove-Item $cmakeCache -Force
    }
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
        $Port = Detect-Port
        if (-not $Port) {
            Die "找不到串口设备，请用 -Port 指定"
        }
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
            & python $captureScript --port $Port --duration $CaptureSeconds 2>&1 | Tee-Object -FilePath $logFile
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
