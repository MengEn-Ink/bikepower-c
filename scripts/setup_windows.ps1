# BikePower C - Windows 编译环境一键配置脚本
# 功能：
#   1. 配置 Windows Defender 排除项目录（加速编译 20-30%）
#   2. 设置 ESP-IDF 环境变量（用户级，永久生效）
#   3. 检查/安装 ESP-IDF 工具链
#   4. 验证 ccache 可用性
#
# 用法:
#   以管理员身份运行（配置 Defender 排除项需要管理员权限）
#   powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1
#
# 非管理员运行时，Defender 排除项配置会跳过（仅提示）

[CmdletBinding()]
param(
    [string]$IdfPath = "D:\work\workspace\esp32-power\esp-idf",
    [switch]$SkipDefender,
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = (Resolve-Path (Join-Path $ScriptDir "..")).Path

Import-Module (Join-Path $ScriptDir "idf_common.psm1") -Force

function Write-Step {
    param([string]$Msg)
    Write-Host ""
    Write-Host "=== $Msg ===" -ForegroundColor Cyan
}

function Test-Admin {
    $currentUser = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    return $currentUser.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)
}

# === 1. 配置 Windows Defender 排除项 ===

Write-Step "1/4 配置 Windows Defender 排除项"

if ($SkipDefender) {
    Write-Host "  跳过 Defender 配置 (-SkipDefender)" -ForegroundColor Yellow
} elseif (-not (Test-Admin)) {
    Write-Host "  当前非管理员权限，无法配置 Defender 排除项" -ForegroundColor Yellow
    Write-Host "  请以管理员身份运行此脚本，或手动排除以下目录：" -ForegroundColor Yellow
    Write-Host "    $ProjectDir"
    Write-Host "    $IdfPath"
    Write-Host "    $env:USERPROFILE\.espressif"
} else {
    $excludePaths = @(
        $ProjectDir
        $IdfPath
        (Join-Path $env:USERPROFILE ".espressif")
    )

    foreach ($p in $excludePaths) {
        if (-not (Test-Path $p)) {
            Write-Host "  跳过不存在的路径: $p" -ForegroundColor Gray
            continue
        }
        try {
            $existing = Get-MpPreference | Select-Object -ExpandProperty ExclusionPath
            if ($existing -contains $p) {
                Write-Host "  已排除: $p" -ForegroundColor Green
            } else {
                Add-MpPreference -ExclusionPath $p
                Write-Host "  已添加排除: $p" -ForegroundColor Green
            }
        } catch {
            Write-Host "  配置失败: $p - $_" -ForegroundColor Red
        }
    }
    Write-Host "  Defender 排除项配置完成" -ForegroundColor Green
}

# === 2. 设置 ESP-IDF 环境变量 ===

Write-Step "2/4 设置 ESP-IDF 环境变量"

if (-not (Test-Path (Join-Path $IdfPath "tools\idf.py"))) {
    Write-Fail "ESP-IDF 源码不存在: $IdfPath"
    Write-Host "  请指定正确的 ESP-IDF 路径: -IdfPath <path>"
    exit 1
}

# 设置用户级环境变量（永久生效，不影响系统级）
$envVars = @{
    "IDF_PATH" = $IdfPath
    "IDF_TOOLS_PATH" = (Join-Path $env:USERPROFILE ".espressif")
}

foreach ($kv in $envVars.GetEnumerator()) {
    $current = [Environment]::GetEnvironmentVariable($kv.Key, "User")
    if ($current -eq $kv.Value) {
        Write-Host "  $($kv.Key) 已设置: $($kv.Value)" -ForegroundColor Green
    } else {
        [Environment]::SetEnvironmentVariable($kv.Key, $kv.Value, "User")
        # 同时设置当前会话
        Set-Item -Path ("Env:" + $kv.Key) -Value $kv.Value
        Write-Host "  已设置 $($kv.Key) = $($kv.Value)" -ForegroundColor Green
    }
}

# === 3. 检查/安装 ESP-IDF 工具链 ===

Write-Step "3/4 检查 ESP-IDF 工具链"

$espressifDir = Join-Path $env:USERPROFILE ".espressif"
$pythonEnvDir = Join-Path $espressifDir "python_env"

if (Test-Path $pythonEnvDir) {
    $pyEnvs = Get-ChildItem -Path $pythonEnvDir -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName "Scripts\python.exe") }
    if ($pyEnvs) {
        Write-Host "  ESP-IDF Python 环境已安装:" -ForegroundColor Green
        foreach ($env in $pyEnvs) {
            Write-Host "    $($env.Name)"
        }
    } else {
        Write-Host "  Python 环境目录存在但无有效 venv" -ForegroundColor Yellow
    }
} else {
    Write-Host "  ESP-IDF Python 环境未安装" -ForegroundColor Yellow
    if ($SkipInstall) {
        Write-Host "  跳过安装 (-SkipInstall)" -ForegroundColor Yellow
    } else {
        Write-Host "  开始安装 ESP-IDF 工具链（仅 esp32c3，节省空间和时间）..." -ForegroundColor Cyan
        Write-Host "  这可能需要 10-30 分钟，取决于网络速度" -ForegroundColor Yellow
        $installScript = Join-Path $IdfPath "install.ps1"
        if (Test-Path $installScript) {
            try {
                & $installScript esp32c3
                if ($LASTEXITCODE -eq 0) {
                    Write-Host "  ESP-IDF 工具链安装完成" -ForegroundColor Green
                } else {
                    Write-Fail "工具链安装失败 (exit=$LASTEXITCODE)"
                    Write-Host "  请手动运行: $installScript esp32c3"
                }
            } catch {
                Write-Fail "工具链安装异常: $_"
                Write-Host "  请手动运行: $installScript esp32c3"
            }
        } else {
            Write-Fail "找不到 install.ps1: $installScript"
        }
    }
}

# === 4. 验证 ccache ===

Write-Step "4/4 验证 ccache"

$ccachePath = Join-Path $espressifDir "tools\ccache\*\ccache.exe"
$ccacheExe = Get-ChildItem -Path $ccachePath -ErrorAction SilentlyContinue | Select-Object -First 1

if ($ccacheExe) {
    Write-Host "  ccache 已安装: $($ccacheExe.FullName)" -ForegroundColor Green
    # 验证版本
    try {
        $version = & $ccacheExe.FullName --version 2>&1 | Select-Object -First 1
        Write-Host "  版本: $version" -ForegroundColor Gray
    } catch {}
} else {
    Write-Host "  ccache 未安装" -ForegroundColor Yellow
    Write-Host "  ccache 会随 ESP-IDF 工具链一起安装（install.ps1）" -ForegroundColor Gray
    Write-Host "  安装完成后项目脚本会通过 IDF_CCACHE_ENABLE=1 自动启用" -ForegroundColor Gray
}

# === 汇总 ===

Write-Step "配置完成"

Write-Host ""
Write-Host "环境变量已设置（重启 PowerShell 生效）：" -ForegroundColor Cyan
Write-Host "  IDF_PATH = $IdfPath"
Write-Host "  IDF_TOOLS_PATH = $espressifDir"
Write-Host ""
Write-Host "下一步：" -ForegroundColor Cyan
Write-Host "  1. 重启 PowerShell（让环境变量生效）"
Write-Host "  2. 激活 ESP-IDF: . $IdfPath\export.ps1"
Write-Host "  3. 编译固件: .\scripts\dev.ps1 build"
Write-Host "  4. 完整 release: .\scripts\build_all.ps1"
Write-Host ""
Write-Host "编译速度优化已启用：" -ForegroundColor Cyan
Write-Host "  - ccache（检测到后设置 IDF_CCACHE_ENABLE=1）"
Write-Host "  - Ninja（ESP-IDF 默认）"
if (-not $SkipDefender -and (Test-Admin)) {
    Write-Host "  - Windows Defender 排除项"
}
Write-Host ""
Write-Host "首次编译会较慢（填 ccache 缓存），后续增量编译会快 50-80%" -ForegroundColor Yellow
