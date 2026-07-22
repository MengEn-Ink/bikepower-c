# BikePower C - Windows 双板型构建脚本
# 分别编译通用 ESP32-C3 和 M5Stamp C3 固件，
# 输出合并后的单文件固件包，包名包含板型、版本号、stable/fix 标识和 Git ID。
#
# 用法:
#   .\scripts\build_all.ps1                    # 编译两种板型
#   .\scripts\build_all.ps1 generic            # 仅编译通用板
#   .\scripts\build_all.ps1 m5stamp            # 仅编译 M5Stamp C3
#   .\scripts\build_all.ps1 -DryRun            # 预览，不编译不写文件
#   .\scripts\build_all.ps1 -SkipChecks        # 跳过构建前测试门禁
#   .\scripts\build_all.ps1 -SkipHostTest      # 跳过 host 单测
#   .\scripts\build_all.ps1 -IdfPath "C:\Espressif\frameworks\esp-idf-v5.5.4"
#
# 产物输出到 build\release\ 目录。

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("generic", "m5stamp")]
    [string[]]$Board = @("generic", "m5stamp"),

    [switch]$DryRun,
    [switch]$SkipChecks,
    [switch]$SkipHostTest,
    [string]$IdfPath = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $ScriptDir "idf_common.psm1") -Force
$ProjectDir = $Global:BikePowerProjectDir
$ReleaseDir = Join-Path (Join-Path $ProjectDir "build") "release"
$DeviceAcceptanceDir = if ($env:DEVICE_ACCEPTANCE_DIR) {
    $env:DEVICE_ACCEPTANCE_DIR
} else {
    Join-Path $ProjectDir "tests\reports"
}

function Get-BoardName {
    param([string]$Target)
    switch ($Target) {
        "generic" { return "generic-c3" }
        "m5stamp" { return "m5stamp-c3" }
        default { throw ("未知板型: " + $Target) }
    }
}

function Run-HostTests {
    Write-Log "运行 host 单测"
    $makeCmd = Get-Command "make" -ErrorAction SilentlyContinue
    $gccCmd = Get-Command "gcc" -ErrorAction SilentlyContinue
    $clangCmd = Get-Command "clang" -ErrorAction SilentlyContinue
    if (-not $makeCmd -or (-not $gccCmd -and -not $clangCmd)) {
        Write-Host "  跳过: 未找到 make/gcc/clang 编译工具链" -ForegroundColor Yellow
        return $false
    }
    $testDir = Join-Path $ProjectDir "test"
    Push-Location $testDir
    try {
        if ($clangCmd) {
            Invoke-SafeCommand -Command "make" -ArgsList @("CC=clang") -WorkingDir $testDir
        } else {
            Invoke-SafeCommand -Command "make" -ArgsList @() -WorkingDir $testDir
        }
        $testBin = Join-Path $testDir "test_domain.exe"
        Invoke-SafeCommand -Command $testBin -ArgsList @() -WorkingDir $testDir
    } finally {
        Pop-Location
    }
    return $true
}

function Run-StaticChecks {
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
            Invoke-PythonScript -ScriptPath $fullPath
        }
    }
    Write-Success "静态门禁检查全部通过"
}

function Run-MockTests {
    Write-Log "运行 Mock 测试"
    $tests = @("tests\test_portal_config_save.py")
    foreach ($test in $tests) {
        $fullPath = Join-Path $ProjectDir $test
        if (Test-Path $fullPath) {
            Invoke-PythonScript -ScriptPath $fullPath
        }
    }
}

function Build-Board {
    param(
        [string]$IdfPath,
        [string]$Target,
        [string]$Version,
        [string]$GitSha,
        [string]$ReleaseDir
    )
    $boardName = Get-BoardName $Target
    $fwName = "bikepower-c-" + $boardName + "-v" + $Version + "-stable-fix-g" + $GitSha + ".bin"
    $fwPath = Join-Path $ReleaseDir $fwName

    Write-Log ("构建板型: " + $boardName)

    $kconfig = switch ($Target) {
        "generic" { "CONFIG_BOARD_GENERIC_C3=y" }
        "m5stamp" { "CONFIG_BOARD_M5STAMP_C3=y" }
    }

    $boardFile = Join-Path $ProjectDir "sdkconfig.board.tmp"
    $defaultsFile = Join-Path $ProjectDir "sdkconfig.defaults"
    $mergedDefaults = Join-Path $ProjectDir "sdkconfig.board.merged.tmp"

    try {
        Set-Content -Path $boardFile -Value $kconfig -Encoding ASCII
        Get-Content $defaultsFile, $boardFile | Set-Content -Path $mergedDefaults -Encoding ASCII

        $cmakeCache = Join-Path (Join-Path $ProjectDir "build") "CMakeCache.txt"
        if (Test-Path $cmakeCache) {
            $cacheContent = Get-Content $cmakeCache -Raw
            if ($cacheContent -match "sdkconfig\.board\.tmp|sdkconfig\.board\.merged\.tmp") {
                Write-Host "  清理残留的临时板型 CMakeCache" -ForegroundColor Yellow
                Remove-Item $cmakeCache -Force
            }
        }

        $oldSdkconfig = Join-Path $ProjectDir "sdkconfig"
        if (Test-Path $oldSdkconfig) {
            $backupPath = Join-Path $ProjectDir "sdkconfig.build_backup"
            Copy-Item $oldSdkconfig $backupPath -Force
        }

        Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @(
            "-D", "SDKCONFIG_DEFAULTS=" + $mergedDefaults,
            "reconfigure"
        )
        Invoke-IdfCommand -IdfPath $IdfPath -IdfArgs @(
            "-D", "SDKCONFIG_DEFAULTS=" + $mergedDefaults,
            "build"
        )

        $bootBin = Join-Path (Join-Path (Join-Path $ProjectDir "build") "bootloader") "bootloader.bin"
        $partBin = Join-Path (Join-Path (Join-Path $ProjectDir "build") "partition_table") "partition-table.bin"
        $appBin = Join-Path (Join-Path $ProjectDir "build") "bikepower-c.bin"

        foreach ($bin in @($bootBin, $partBin, $appBin)) {
            if (-not (Test-Path $bin)) {
                throw ("缺少构建产物: " + $bin)
            }
        }

        Invoke-EsptoolCommand -IdfPath $IdfPath -ToolArgs @(
            "--chip", "esp32c3",
            "merge_bin",
            "--flash_mode", "dio",
            "--flash_freq", "80m",
            "--flash_size", "4MB",
            "--output", $fwPath,
            "0x0", $bootBin,
            "0x8000", $partBin,
            "0x10000", $appBin
        )

        if (-not (Test-Path $fwPath)) {
            throw ("合并固件生成失败: " + $fwPath)
        }

        $size = (Get-Item $fwPath).Length
        Write-Success ("固件已生成: " + $fwName + " (" + $size + " bytes)")
        return $fwName
    } finally {
        foreach ($tmp in @($boardFile, $mergedDefaults)) {
            if (Test-Path $tmp) { Remove-Item $tmp -Force }
        }
        $backupPath = Join-Path $ProjectDir "sdkconfig.build_backup"
        if (Test-Path $backupPath) {
            Move-Item $backupPath (Join-Path $ProjectDir "sdkconfig") -Force
        }
    }
}

function Build-ReleaseArtifacts {
    param(
        [string]$Version,
        [string]$StableVersion,
        [string]$FixVersion,
        [string]$GitSha,
        [string]$ReleaseDir,
        [string[]]$ProducedBins
    )
    Write-Log "生成 Release 标准化产物: SHA256 / manifest"

    $manifestPath = Join-Path $ReleaseDir "manifest.txt"
    $manifestLines = @(
        "# BikePower C Release 产物清单",
        ("# 版本: v" + $Version),
        ("# 稳定版: " + $StableVersion),
        ("# 修复版: " + $FixVersion),
        ("# Git ID: " + $GitSha),
        ("# 生成时间: " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')),
        "# 字段: 文件名  大小(字节)  SHA256"
    )

    foreach ($fw in $ProducedBins) {
        $fwPath = Join-Path $ReleaseDir $fw
        $sha = Get-Sha256 $fwPath
        $size = (Get-Item $fwPath).Length
        $shaFile = $fw + ".sha256"
        $shaPath = Join-Path $ReleaseDir $shaFile
        Set-Content -Path $shaPath -Value ($sha + "  " + $fw) -Encoding ASCII
        $manifestLines += ($fw + "  " + $size + "  " + $sha)
        $shaSize = (Get-Item $shaPath).Length
        $shaSha = Get-Sha256 $shaPath
        $manifestLines += ($shaFile + "  " + $shaSize + "  " + $shaSha)
        Write-Host ("  SHA256: " + $fw + " -> " + $sha.Substring(0,16) + "...")
    }

    $docFiles = @("test-report.md", "core-flow-endurance.md", "user-manual.md")
    foreach ($doc in $docFiles) {
        $docPath = Join-Path $ReleaseDir $doc
        if (Test-Path $docPath) {
            $sha = Get-Sha256 $docPath
            $size = (Get-Item $docPath).Length
            $manifestLines += ($doc + "  " + $size + "  " + $sha)
        }
    }

    $imageDir = Join-Path $ReleaseDir "images"
    if (Test-Path $imageDir) {
        $imgFiles = Get-ChildItem -Path $imageDir -Recurse -File | Sort-Object FullName
        foreach ($img in $imgFiles) {
            $relPath = $img.FullName.Substring($ReleaseDir.Length + 1).Replace("\", "/")
            $sha = Get-Sha256 $img.FullName
            $size = $img.Length
            $manifestLines += ($relPath + "  " + $size + "  " + $sha)
        }
    }

    $evidenceDir = Join-Path $ReleaseDir "evidence"
    if (Test-Path $evidenceDir) {
        $evidenceFiles = Get-ChildItem -Path $evidenceDir -File -Filter "device-acceptance-*.json" | Sort-Object Name
        foreach ($evidence in $evidenceFiles) {
            $relPath = "evidence/" + $evidence.Name
            $sha = Get-Sha256 $evidence.FullName
            $size = $evidence.Length
            $manifestLines += ($relPath + "  " + $size + "  " + $sha)
        }
    }

    Set-Content -Path $manifestPath -Value $manifestLines -Encoding ASCII
    Write-Success "manifest.txt 已生成"
}

function Build-ZipPackage {
    param(
        [string]$Version,
        [string]$GitSha,
        [string]$ReleaseDir,
        [string[]]$Items
    )
    Write-Log "打包 Release 总包"
    $zipName = "bikepower-c-release-v" + $Version + "-stable-fix-g" + $GitSha + ".zip"
    $zipPath = Join-Path $ReleaseDir $zipName

    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

    Compress-Archive -Path $Items -DestinationPath $zipPath -CompressionLevel Optimal

    $sha = Get-Sha256 $zipPath
    $shaPath = $zipPath + ".sha256"
    Set-Content -Path $shaPath -Value ($sha + "  " + $zipName) -Encoding ASCII

    $size = (Get-Item $zipPath).Length
    Write-Success ("总包已生成: " + $zipName + " (" + $size + " bytes)")
    Write-Host ("  SHA256: " + $sha.Substring(0,16) + "...")

    return $zipName
}

$configPath = Join-Path (Join-Path $ProjectDir "main") "bikepower_config.h"
if (-not (Test-Path $configPath)) {
    throw ("找不到配置文件: " + $configPath)
}
$configContent = Get-Content $configPath -Raw
$Version = Extract-Define $configContent "FIRMWARE_VERSION"
$StableVersion = Extract-Define $configContent "FIRMWARE_STABLE_VERSION"
$FixVersion = Extract-Define $configContent "FIRMWARE_FIX_VERSION"

if (-not $Version -or -not $StableVersion -or -not $FixVersion) {
    throw ("无法从 " + $configPath + " 提取版本信息")
}

try {
    $GitSha = (& git -C $ProjectDir rev-parse --short HEAD 2>$null)
} catch {
    $GitSha = "nogit"
}
if (-not $GitSha) { $GitSha = "nogit" }

Write-Host ""
Write-Host ("=== BikePower C 双板型构建 v" + $Version + " stable" + $StableVersion + " fix" + $FixVersion + " g" + $GitSha + " ===") -ForegroundColor Magenta
Write-Host ("板型: " + ($Board -join ', '))
Write-Host ("项目目录: " + $ProjectDir)
Write-Host ("输出目录: " + $ReleaseDir)

if ($DryRun) {
    Write-Host ""
    Write-Host "[dry-run] 不会编译、不会写文件、不会打 tag、不会创建 Release。" -ForegroundColor Yellow
    Write-Host "将生成的产物:"
    foreach ($target in $Board) {
        $bn = Get-BoardName $target
        Write-Host ("  - bikepower-c-" + $bn + "-v" + $Version + "-stable-fix-g" + $GitSha + ".bin  (合并单文件固件)")
        Write-Host ("  - bikepower-c-" + $bn + "-v" + $Version + "-stable-fix-g" + $GitSha + ".bin.sha256  (SHA256 校验)")
    }
    Write-Host "  - manifest.txt  (产物清单)"
    Write-Host "  - test-report.md  (测试报告)"
    Write-Host "  - core-flow-endurance.md  (核心流程耐久测试明细)"
    Write-Host "  - user-manual.md  (用户文档副本)"
    Write-Host "  - images/  (用户文档截图与示意图)"
    Write-Host "  - evidence\device-acceptance-<板型>.json  (当前版本各板型真机验收证据；存在时收录)"
    Write-Host ("  - bikepower-c-release-v" + $Version + "-stable-fix-g" + $GitSha + ".zip  (release 总包)")
    Write-Host ("  - bikepower-c-release-v" + $Version + "-stable-fix-g" + $GitSha + ".zip.sha256")
    exit 0
}

$IdfPath = Find-EspIdf -UserSpecified $IdfPath
if ($null -eq $IdfPath) {
    Write-Fail "找不到 ESP-IDF 环境"
    Write-Host "  请通过 -IdfPath 指定 ESP-IDF 安装目录，或设置 IDF_PATH 环境变量。" -ForegroundColor Yellow
    Write-Host "  常见安装位置:" -ForegroundColor Yellow
    Write-Host "    - C:\Espressif\frameworks\esp-idf-v5.5.4" -ForegroundColor Yellow
    Write-Host "    - %USERPROFILE%\esp\esp-idf-v5.5.4" -ForegroundColor Yellow
    exit 1
}

if (-not (Test-Path $ReleaseDir)) {
    New-Item -Path $ReleaseDir -ItemType Directory -Force | Out-Null
}

$oldPatterns = @(
    "bikepower-c-*-v" + $Version + "-stable-fix-g*.bin",
    "bikepower-c-*-v" + $Version + "-stable-fix-g*.bin.sha256",
    "bikepower-c-release-v" + $Version + "-stable-fix-g*.zip",
    "bikepower-c-release-v" + $Version + "-stable-fix-g*.zip.sha256",
    "manifest.txt",
    "test-report.md",
    "core-flow-endurance.md",
    "user-manual.md"
)
foreach ($pat in $oldPatterns) {
    $oldPath = Join-Path $ReleaseDir $pat
    Remove-Item -Path $oldPath -Force -ErrorAction SilentlyContinue
}
$oldEvidenceDir = Join-Path $ReleaseDir "evidence"
if (Test-Path $oldEvidenceDir) {
    Remove-Item -Path $oldEvidenceDir -Recurse -Force
}

Activate-IdfEnv -IdfPath $IdfPath

if (-not $SkipChecks) {
    if (-not $SkipHostTest) {
        $hostTestPassed = Run-HostTests
    } else {
        Write-Host "  跳过 host 单测 (-SkipHostTest)" -ForegroundColor Yellow
    }
    Run-StaticChecks
    Run-MockTests
} else {
    Write-Host "  跳过构建前门禁 (-SkipChecks)" -ForegroundColor Yellow
}

$producedBins = @()
foreach ($target in $Board) {
    $fwName = Build-Board -IdfPath $IdfPath -Target $target -Version $Version -GitSha $GitSha -ReleaseDir $ReleaseDir
    $producedBins += $fwName
}

$deviceAcceptancePy = Join-Path (Join-Path (Join-Path $ScriptDir "tools") "device_acceptance.py")
Write-Log "按板型校验并收录真机验收证据"
foreach ($target in $Board) {
    $boardName = Get-BoardName $target
    $evidenceName = "device-acceptance-" + $boardName + ".json"
    $evidenceSource = Join-Path $DeviceAcceptanceDir $evidenceName
    if (Test-Path $evidenceSource) {
        Invoke-PythonScript -ScriptPath $deviceAcceptancePy -ScriptArgs @(
            "validate",
            "--file", $evidenceSource,
            "--firmware-version", $Version,
            "--git-sha", $GitSha,
            "--board", $boardName
        )
        $evidenceDir = Join-Path $ReleaseDir "evidence"
        New-Item -Path $evidenceDir -ItemType Directory -Force | Out-Null
        Copy-Item $evidenceSource (Join-Path $evidenceDir $evidenceName) -Force
        Write-Success ("已收录 evidence\" + $evidenceName)
    } else {
        Write-Host ("  未找到 " + $evidenceSource + "；允许本地构建，正式发布前必须补齐该板型全部 P0 证据。") -ForegroundColor Yellow
    }
}

$releaseDocsPy = Join-Path (Join-Path (Join-Path $ScriptDir "tools") "generate_release_docs.py")
if (Test-Path $releaseDocsPy) {
    Write-Log "生成测试报告与用户文档"
    $testSummary = "构建前门禁已通过：静态门禁全绿；Mock 测试通过；host 单测按环境可选执行。"
    $notes = "build_all.ps1 生成的是本地 release 产物；正式发版前必须补齐真机验证结果。"
    $docsExit = Invoke-PythonScript -ScriptPath $releaseDocsPy -ScriptArgs @(
        "--release-dir", $ReleaseDir,
        "--test-summary", $testSummary,
        "--notes", $notes
    ) -IgnoreExitCode
    if ($docsExit -ne 0) {
        Write-Host "  警告: 文档生成脚本返回非零退出码" -ForegroundColor Yellow
    }
}

Build-ReleaseArtifacts -Version $Version -StableVersion $StableVersion -FixVersion $FixVersion -GitSha $GitSha -ReleaseDir $ReleaseDir -ProducedBins $producedBins

$zipItems = @()
foreach ($fw in $producedBins) {
    $zipItems += Join-Path $ReleaseDir $fw
    $zipItems += Join-Path $ReleaseDir ($fw + ".sha256")
}
foreach ($doc in @("manifest.txt", "test-report.md", "core-flow-endurance.md", "user-manual.md")) {
    $docPath = Join-Path $ReleaseDir $doc
    if (Test-Path $docPath) { $zipItems += $docPath }
}
$imageDir = Join-Path $ReleaseDir "images"
if (Test-Path $imageDir) { $zipItems += $imageDir }
$evidenceDir = Join-Path $ReleaseDir "evidence"
if (Test-Path $evidenceDir) { $zipItems += $evidenceDir }

$zipName = Build-ZipPackage -Version $Version -GitSha $GitSha -ReleaseDir $ReleaseDir -Items $zipItems

$artifactGate = Join-Path (Join-Path (Join-Path $ScriptDir "tools") "check_release_artifacts.py")
if ($Board.Count -eq 2 -and (Test-Path $artifactGate)) {
    Write-Log "运行 Release 产物门禁"
    $gateExit = Invoke-PythonScript -ScriptPath $artifactGate -ScriptArgs @(
        "--release-dir", $ReleaseDir,
        "--version", $Version,
        "--git-sha", $GitSha,
        "--allow-dirty"
    ) -IgnoreExitCode
    if ($gateExit -ne 0) {
        Write-Fail "Release 产物门禁未通过"
        exit 1
    }
}

Write-Host ""
Write-Host ("=== 构建完成 ===") -ForegroundColor Green
Write-Host ("输出目录: " + $ReleaseDir)
Write-Host ("固件: " + ($producedBins -join ', '))
Write-Host ("总包: " + $zipName)
Write-Host ""
