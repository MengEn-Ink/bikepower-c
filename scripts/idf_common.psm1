# BikePower C - Windows PowerShell 共享模块
# 统一封装 ESP-IDF 探测/激活、Python 环境探测、子进程调用、串口探测等通用逻辑。
# 消除 build_all.ps1 / flash_latest.ps1 / dev.ps1 之间的重复代码。
#
# 用法:
#   Import-Module (Join-Path $PSScriptRoot "idf_common.psm1") -Force
#   $idf = Find-EspIdf
#   Activate-IdfEnv -IdfPath $idf
#   $py = Get-IdfPython
#
# 设计原则:
#   - 不硬编码 ESP-IDF 版本号，用 Get-ChildItem 扫描目录
#   - 不硬编码 Python venv 名称，用通配符扫描
#   - 子进程输出用异步读取避免管道死锁
#   - 所有 Python 调用统一走 Get-IdfPython 返回的路径

# 项目根目录（模块位于 scripts/，向上一级）
$Global:BikePowerProjectDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

# === 日志输出 ===

function Write-Log {
    param([string]$Message)
    Write-Host ""
    Write-Host ("==> " + $Message) -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host ("[OK] " + $Message) -ForegroundColor Green
}

function Write-Fail {
    param([string]$Message)
    Write-Host ("[FAIL] " + $Message) -ForegroundColor Red
}

function Write-Warn {
    param([string]$Message)
    Write-Host ("[WARN] " + $Message) -ForegroundColor Yellow
}

function Die {
    param([string]$Message)
    Write-Fail $Message
    exit 1
}

# === ESP-IDF 环境探测 ===

# 扫描指定父目录下所有 esp-idf* 子目录，按版本号降序返回
function Get-IdfCandidatesFromDir {
    param([string]$ParentDir)
    if (-not (Test-Path $ParentDir)) { return @() }
    $dirs = Get-ChildItem -Path $ParentDir -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match "^esp-idf" -and (Test-Path (Join-Path $_.FullName "tools\idf.py")) }
    # 按目录名降序（esp-idf-v5.5.4 > esp-idf-v5.5.1 > esp-idf）
    return ($dirs | Sort-Object Name -Descending | ForEach-Object { $_.FullName })
}

# 探测 ESP-IDF 安装路径
# 优先级：用户指定 > IDF_PATH 环境变量 > 候选目录扫描 > PATH 中的 idf.py
# 返回：
#   - 路径字符串（找到）
#   - ""（空字符串，表示当前 shell 已激活 idf.py 可用）
#   - $null（未找到）
function Find-EspIdf {
    param([string]$UserSpecified = "")

    # 1. 用户显式指定
    if ($UserSpecified) {
        if (Test-Path (Join-Path $UserSpecified "tools\idf.py")) {
            return (Resolve-Path $UserSpecified).Path
        }
        if (Test-Path (Join-Path $UserSpecified "export.ps1")) {
            return (Resolve-Path $UserSpecified).Path
        }
        Write-Warn "用户指定的 IDF 路径无效: $UserSpecified"
    }

    # 2. IDF_PATH 环境变量
    if ($env:IDF_PATH -and (Test-Path (Join-Path $env:IDF_PATH "tools\idf.py"))) {
        return $env:IDF_PATH
    }

    # 3. 扫描常见安装目录（不硬编码版本号）
    $scanRoots = @(
        "C:\Espressif\frameworks",
        "D:\Espressif\frameworks",
        "E:\Espressif\frameworks",
        (Join-Path $env:USERPROFILE "esp"),
        (Join-Path $env:USERPROFILE ".espressif\frameworks"),
        (Join-Path $env:USERPROFILE "workspace"),
        "D:\work\workspace",
        "D:\workspace",
        "C:\esp",
        "D:\esp",
        "C:\esp-idf",
        "D:\esp-idf"
    )
    foreach ($root in $scanRoots) {
        $candidates = Get-IdfCandidatesFromDir -ParentDir $root
        if ($candidates -and $candidates.Count -gt 0) {
            return $candidates[0]
        }
    }

    # 4. 检查 PATH 中是否已有 idf.py（表示 shell 已激活）
    if (Get-Command "idf.py" -ErrorAction SilentlyContinue) {
        return ""
    }

    return $null
}

# 探测 ESP-IDF 对应的 Python 解释器路径
# 优先级：IDF_PYTHON_ENV_PATH 环境变量 > 扫描 .espressif\python_env 目录 > 系统 PATH 的 python
function Get-IdfPython {
    # 1. 环境变量
    $pyEnv = $env:IDF_PYTHON_ENV_PATH
    if ($pyEnv) {
        $pyExe = Join-Path $pyEnv "Scripts\python.exe"
        if (Test-Path $pyExe) { return $pyExe }
    }

    # 2. 扫描 .espressif\python_env 下的 idf*_env 目录（通配符，不硬编码版本）
    $pyEnvRoots = @(
        (Join-Path $env:USERPROFILE ".espressif\python_env"),
        "C:\Espressif\python_env",
        "D:\Espressif\python_env"
    )
    foreach ($root in $pyEnvRoots) {
        if (-not (Test-Path $root)) { continue }
        # 按目录名降序排列，优先 idf5.5_py3.12 > idf5.5_py3.11 > idf5.5_py3.9
        $envDirs = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match "^idf.*_env$" } |
            Sort-Object Name -Descending
        foreach ($d in $envDirs) {
            $pyExe = Join-Path $d.FullName "Scripts\python.exe"
            if (Test-Path $pyExe) { return $pyExe }
        }
    }

    # 3. 回退到 PATH 中的 python
    return "python"
}

# 激活 ESP-IDF 环境（source export.ps1）
function Activate-IdfEnv {
    param([string]$IdfPath)
    if (-not $IdfPath) {
        # 验证 idf.py 是否真的在 PATH 中
        if (-not (Get-Command "idf.py" -ErrorAction SilentlyContinue)) {
            Die "idf.py 不在 PATH 中，ESP-IDF 环境未激活。请用 -IdfPath 指定 ESP-IDF 安装路径。"
        }
        Write-Log "使用当前 shell 中已激活的 ESP-IDF"
        return
    }
    Write-Log ("激活 ESP-IDF: " + $IdfPath)
    $exportScript = Join-Path $IdfPath "export.ps1"
    if (-not (Test-Path $exportScript)) {
        Die "找不到 ESP-IDF 导出脚本: $exportScript"
    }
    # 保存当前 ErrorActionPreference，export.ps1 内部可能有非致命错误
    $oldEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        . $exportScript
    } catch {
        $ErrorActionPreference = $oldEAP
        Die "ESP-IDF 激活失败: $_"
    }
    $ErrorActionPreference = $oldEAP

    # 验证激活是否成功
    if (-not $env:IDF_PATH) {
        Die "ESP-IDF 激活失败: IDF_PATH 未设置。可能 Python venv 或工具链未安装。`n  请运行 install.ps1 安装 ESP-IDF 工具链。"
    }
    Write-Success ("ESP-IDF 已激活: IDF_PATH=" + $env:IDF_PATH)
}

# === 子进程调用 ===

# 安全执行子进程，输出直接打印到控制台
# 使用 Start-Process 同步等待，避免管道死锁和闭包问题
function Invoke-SafeCommand {
    param(
        [string]$Command,
        [string[]]$ArgsList = @(),
        [string]$WorkingDir = $Global:BikePowerProjectDir,
        [switch]$IgnoreExitCode
    )
    $argStr = $ArgsList -join ' '
    Write-Host ("+ " + $Command + " " + $argStr) -ForegroundColor Gray

    # 使用 Start-Process 同步执行，输出直接继承控制台
    # 避免 ProcessStartInfo + 异步事件的闭包问题和编码问题
    $procParams = @{
        FilePath = $Command
        ArgumentList = $ArgsList
        WorkingDirectory = $WorkingDir
        NoNewWindow = $true
        Wait = $true
        PassThru = $true
    }
    $proc = Start-Process @procParams
    if ($proc.ExitCode -ne 0 -and -not $IgnoreExitCode) {
        throw ("命令失败 (exit=" + $proc.ExitCode + "): " + $Command + " " + $argStr)
    }
    return $proc.ExitCode
}

# 调用 idf.py
function Invoke-IdfCommand {
    param(
        [string]$IdfPath,
        [string[]]$IdfArgs = @(),
        [string]$WorkingDir = $Global:BikePowerProjectDir
    )
    if ($IdfPath) {
        # 指定了 ESP-IDF 路径：用 python 执行 idf.py
        $pythonExe = Get-IdfPython
        $idfPy = Join-Path $IdfPath "tools\idf.py"
        if (-not (Test-Path $idfPy)) {
            Die "找不到 idf.py: $idfPy"
        }
        $allArgs = @($idfPy) + $IdfArgs
        return Invoke-SafeCommand -Command $pythonExe -ArgsList $allArgs -WorkingDir $WorkingDir
    } else {
        # 当前 shell 已激活 ESP-IDF：idf.py 在 PATH 中，直接调用
        return Invoke-SafeCommand -Command "idf.py" -ArgsList $IdfArgs -WorkingDir $WorkingDir
    }
}

# 调用 esptool.py
function Invoke-EsptoolCommand {
    param(
        [string]$IdfPath,
        [string[]]$ToolArgs = @(),
        [string]$WorkingDir = $Global:BikePowerProjectDir
    )
    if ($IdfPath) {
        $pythonExe = Get-IdfPython
        $esptoolPy = Join-Path $IdfPath "components\esptool_py\esptool\esptool.py"
        if (-not (Test-Path $esptoolPy)) {
            Die "找不到 esptool.py: $esptoolPy"
        }
        $allArgs = @($esptoolPy) + $ToolArgs
        return Invoke-SafeCommand -Command $pythonExe -ArgsList $allArgs -WorkingDir $WorkingDir
    } else {
        # 当前 shell 已激活：esptool.py 在 PATH 中
        return Invoke-SafeCommand -Command "esptool.py" -ArgsList $ToolArgs -WorkingDir $WorkingDir
    }
}

# 调用 Python 脚本（统一走 IDF Python 环境）
function Invoke-PythonScript {
    param(
        [string]$ScriptPath,
        [string[]]$ScriptArgs = @(),
        [string]$WorkingDir = $Global:BikePowerProjectDir,
        [switch]$IgnoreExitCode
    )
    $pythonExe = Get-IdfPython
    $allArgs = @($ScriptPath) + $ScriptArgs
    return Invoke-SafeCommand -Command $pythonExe -ArgsList $allArgs -WorkingDir $WorkingDir -IgnoreExitCode:$IgnoreExitCode
}

# === 串口探测 ===

# 探测 ESP32-C3 串口设备
# 优先级：pyserial 评分 > .NET SerialPort 兜底
# 返回串口设备名（如 COM3）或 $null
function Detect-SerialPort {
    $detected = ""

    # 第一级：pyserial 按评分排序
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
    if "ttyACM" in device or "ttyUSB" in device or "usbmodem" in device or "usbserial" in device:
        return (3, device)
    return (9, device)

candidates = [p for p in ports if score(p)[0] < 9]
if candidates:
    print(sorted(candidates, key=score)[0].device)
'@
        $pythonExe = Get-IdfPython
        $detected = (& $pythonExe -c $pyscript 2>$null)
        if ($LASTEXITCODE -ne 0) { $detected = "" }
    } catch {}

    if ($detected) {
        return $detected
    }

    # 第二级：.NET SerialPort 兜底
    try {
        $comPorts = [System.IO.Ports.SerialPort]::GetPortNames()
        if ($comPorts -and $comPorts.Count -gt 0) {
            # 取第一个 COM 口
            return $comPorts[0]
        }
    } catch {}

    return $null
}

# 校验串口是否存在（Windows 上检查 COM 口是否在端口列表中）
function Test-SerialPort {
    param([string]$PortName)
    if (-not $PortName) { return $false }
    try {
        $ports = [System.IO.Ports.SerialPort]::GetPortNames()
        return ($ports -contains $PortName)
    } catch {
        return $true
    }
}

# === 工具函数 ===

# 从 C 头文件提取 #define 值
function Extract-Define {
    param([string]$Src, [string]$Name)
    $pattern = "#define\s+" + $Name + "\s+`"([^`"]+)`""
    if ($Src -match $pattern) {
        return $Matches[1]
    }
    return ""
}

# 计算 SHA256
function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLower()
}

# 清理 build/CMakeCache.txt 中残留的临时板型配置
function Clear-StaleBoardCache {
    param([string]$ProjectDir = $Global:BikePowerProjectDir)
    $cmakeCache = Join-Path (Join-Path $ProjectDir "build") "CMakeCache.txt"
    if (-not (Test-Path $cmakeCache)) { return }
    $content = Get-Content $cmakeCache -Raw
    if ($content -match "sdkconfig\.board\.tmp|sdkconfig\.flash\.tmp|sdkconfig\.build\.tmp") {
        Write-Host "  清理 build/CMakeCache.txt 中残留的临时板型配置" -ForegroundColor Yellow
        Remove-Item $cmakeCache -Force
    }
}

# 导出公开函数
Export-ModuleMember -Function Write-Log, Write-Success, Write-Fail, Write-Warn, Die, `
    Find-EspIdf, Get-IdfPython, Activate-IdfEnv, `
    Invoke-SafeCommand, Invoke-IdfCommand, Invoke-EsptoolCommand, Invoke-PythonScript, `
    Detect-SerialPort, Test-SerialPort, `
    Extract-Define, Get-Sha256, Clear-StaleBoardCache
