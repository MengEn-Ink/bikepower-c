# Zwift BLE DLL 替换 / 回滚（带备份与校验）
#
# 背景：Zwift 在 MTA 线程上加载 BleWin10Lib.dll，DllMain 要求 STA 套间时返回
# WinError 1114（DLL 初始化例程失败），主库与 fallback 库双双失败，导致
# "Native BLE unavailable"。详见 docs/dev/71-zwift-ble-diagnosis.md。
#
# 用法：
#   pwsh scripts/tools/zwift_ble_dll_swap.ps1 -NewDll "C:\path\to\BleWin10Lib.dll"
#   pwsh scripts/tools/zwift_ble_dll_swap.ps1 -Rollback
#
# 只动 D:\app\Zwift 下的两个 BLE DLL；替换前把原件备份到
# D:\app\Zwift\_ble_dll_backup\<时间戳>\，回滚即原样恢复。

param(
    [string]$NewDll,
    [switch]$Rollback,
    [string]$ZwiftDir = 'D:\app\Zwift'
)

$ErrorActionPreference = 'Stop'

$Target = Join-Path $ZwiftDir 'BleWin10Lib.dll'
$Fallback = Join-Path $ZwiftDir 'BleWin10Lib_fallback.dll'
$BackupRoot = Join-Path $ZwiftDir '_ble_dll_backup'

function Get-Sha256([string]$p) {
    if (-not (Test-Path -LiteralPath $p)) { return '<缺失>' }
    (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
}

if ($Rollback) {
    $dirs = Get-ChildItem -LiteralPath $BackupRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending
    if (-not $dirs) { throw "没有可用备份目录：$BackupRoot" }
    $src = $dirs[0].FullName
    foreach ($name in 'BleWin10Lib.dll', 'BleWin10Lib_fallback.dll') {
        $from = Join-Path $src $name
        if (-not (Test-Path -LiteralPath $from)) { Write-Host "跳过（备份中不存在）：$name"; continue }
        Copy-Item -LiteralPath $from -Destination (Join-Path $ZwiftDir $name) -Force
        Write-Host "已恢复 $name"
    }
    Write-Host ""
    Write-Host ("BleWin10Lib.dll          = " + (Get-Sha256 $Target))
    Write-Host ("BleWin10Lib_fallback.dll = " + (Get-Sha256 $Fallback))
    Write-Host '回滚完成。重启 Zwift 复核日志中的 BLE_LOAD_FAIL。'
    exit 0
}

if (-not $NewDll) { throw '用法：-NewDll <待替换的 BleWin10Lib.dll 路径>，或 -Rollback' }
if (-not (Test-Path -LiteralPath $NewDll)) { throw "待替换文件不存在：$NewDll" }

$running = Get-Process -Name ZwiftApp -ErrorAction SilentlyContinue
if ($running) { throw 'ZwiftApp 正在运行，请先完全退出 Zwift 再替换（DLL 被占用时无法覆盖）' }

# 1) 备份原件（每次替换一个独立时间戳目录，不覆盖历史备份）
$stamp = Get-Date -Format 'yyyy-MM-dd_HHmmss'
$dst = Join-Path $BackupRoot $stamp
New-Item -ItemType Directory -Path $dst -Force | Out-Null
foreach ($name in 'BleWin10Lib.dll', 'BleWin10Lib_fallback.dll') {
    $src = Join-Path $ZwiftDir $name
    if (Test-Path -LiteralPath $src) {
        Copy-Item -LiteralPath $src -Destination (Join-Path $dst $name) -Force
        Write-Host ("备份 {0} → {1}" -f $name, $dst)
    }
}

# 2) 替换前记录哈希
Write-Host ("替换前 BleWin10Lib.dll          = " + (Get-Sha256 $Target))
Write-Host ("替换前 BleWin10Lib_fallback.dll = " + (Get-Sha256 $Fallback))
Write-Host ("候选文件 SHA256                 = " + (Get-Sha256 $NewDll))

# 3) 覆盖主库
Copy-Item -LiteralPath $NewDll -Destination $Target -Force
Write-Host ("替换后 BleWin10Lib.dll          = " + (Get-Sha256 $Target))

# 4) 基本的 PE 架构校验，防止把 32 位 DLL 塞进 64 位程序
$machine = $null
try {
    $bytes = [IO.File]::ReadAllBytes($Target)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
} catch { }
switch ($machine) {
    0x8664  { Write-Host '架构校验：x64，匹配 ZwiftApp.exe' }
    0x014C  { throw '架构校验失败：候选 DLL 是 x86，ZwiftApp.exe 为 x64，已中止（请用 -Rollback 恢复）' }
    default { Write-Host ("架构校验：未识别（0x{0:X4}），请人工确认" -f $machine) }
}

Write-Host ''
Write-Host '替换完成。下一步：启动 Zwift，进入配对页，然后检查最新日志：'
Write-Host '  Get-ChildItem D:\app\Zwift\zoffline\storage\1\logfiles | Sort-Object LastWriteTime -Descending | Select-Object -First 1'
Write-Host '  日志中不应再出现 BLE_LOAD_FAIL / Native BLE unavailable。'
Write-Host '若仍失败：pwsh scripts/tools/zwift_ble_dll_swap.ps1 -Rollback'
