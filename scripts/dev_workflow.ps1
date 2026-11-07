# BikePower C - dev_workflow 薄包装（Windows / PowerShell）
#
# 作用：只负责找到一个 Python 解释器并调用 scripts/tools/dev_workflow.py。
# 设计约束：
#   - 不在本脚本里 source export.ps1、不改系统 PATH；IDF 进程环境由 Python 内部装配。
#   - 子命令全部透传，退出码原样返回。
#
# 用法:
#   .\scripts\dev_workflow.ps1 doctor
#   .\scripts\dev_workflow.ps1 build --mode production --board generic
#   .\scripts\dev_workflow.ps1 build --mode diagnostic --dry-run
#   .\scripts\dev_workflow.ps1 test --mode maintenance --name host -- python tests\test_dev_workflow.py
#   .\scripts\dev_workflow.ps1 status
#   .\scripts\dev_workflow.ps1 clean --mode diagnostic

[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ForwardArgs
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoDir = Split-Path -Parent $ScriptDir
$Entry = Join-Path $ScriptDir "tools" "dev_workflow.py"

if (-not (Test-Path -LiteralPath $Entry)) {
    Write-Error "找不到 dev_workflow 入口: $Entry"
    exit 1
}

# 选解释器：dev_workflow.py 只用标准库，优先系统 python；找不到再用 IDF python。
# 注意：python 在某些机器上是 Windows Store 别名（AliasInfo，Source 为空），必须跳过。
$pythonExe = $null
$sysPy = Get-Command "python" -ErrorAction SilentlyContinue
if ($sysPy -and $sysPy.Source -and (Test-Path -LiteralPath $sysPy.Source)) {
    $pythonExe = $sysPy.Source
} else {
    $idfPyRoot = Join-Path $env:USERPROFILE ".espressif\python_env"
    if (Test-Path -LiteralPath $idfPyRoot) {
        $candidate = Get-ChildItem -LiteralPath $idfPyRoot -Directory -Filter "idf*_env" |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($candidate) {
            $py = Join-Path $candidate.FullName "Scripts\python.exe"
            if (Test-Path -LiteralPath $py) { $pythonExe = $py }
        }
    }
}
if (-not $pythonExe) {
    Write-Error "未找到 Python（系统 PATH 或 ~/.espressif/python_env）。"
    exit 1
}

$allArgs = @($Entry) + $ForwardArgs
& $pythonExe @allArgs
exit $LASTEXITCODE
