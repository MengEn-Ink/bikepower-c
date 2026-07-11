# Windows 环境编译指南

> ESP32-C3 固件在 Windows 下的环境配置、编译流程、速度优化与排障指南。

## 一、环境准备

### 1.1 一次性配置（推荐）

运行项目自带的配置脚本，自动完成 Defender 排除、环境变量设置、工具链检查：

```powershell
# 以管理员身份运行（配置 Defender 排除项需要管理员权限）
powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1
```

脚本执行内容：
1. 配置 Windows Defender 排除项目录（加速编译 20-30%）
2. 设置用户级环境变量 `IDF_PATH` 和 `IDF_TOOLS_PATH`
3. 检查/安装 ESP-IDF 工具链（仅 esp32c3）
4. 验证 ccache 可用性

### 1.2 手动配置（可选）

如果不使用配置脚本，手动步骤如下：

**安装 ESP-IDF 工具链**：

```powershell
cd D:\work\workspace\esp32-power\esp-idf
.\install.ps1 esp32c3
```

**设置环境变量**（用户级，永久生效）：

```powershell
[Environment]::SetEnvironmentVariable("IDF_PATH", "D:\work\workspace\esp32-power\esp-idf", "User")
[Environment]::SetEnvironmentVariable("IDF_TOOLS_PATH", "$env:USERPROFILE\.espressif", "User")
```

**配置 Defender 排除项**（管理员权限）：

```powershell
Add-MpPreference -ExclusionPath "D:\workspace\bikepower-c"
Add-MpPreference -ExclusionPath "D:\work\workspace\esp32-power\esp-idf"
Add-MpPreference -ExclusionPath "$env:USERPROFILE\.espressif"
```

### 1.3 激活 ESP-IDF 环境

每次打开新的 PowerShell 终端，编译前需要激活 ESP-IDF：

```powershell
. $env:IDF_PATH\export.ps1
```

或者使用项目脚本（自动探测并激活）：

```powershell
.\scripts\dev.ps1 build   # 自动探测并激活 ESP-IDF
```

## 二、编译流程

### 2.1 日常开发编译（增量编译最快）

```powershell
.\scripts\dev.ps1 build
```

流程：探测 ESP-IDF → 激活 → 静态门禁 → `idf.py reconfigure + build`

输出：`build\bikepower-c.bin` 等固件产物

### 2.2 指定板型编译

```powershell
.\scripts\dev.ps1 build -Board m5stamp
```

板型选项：`generic`（默认）、`m5stamp`

### 2.3 跳过静态门禁（快速迭代）

```powershell
.\scripts\dev.ps1 build -NoChecks
```

适用于：已通过门禁，仅改了代码想快速看编译是否通过。

### 2.4 完整 Release 构建（双板型）

```powershell
.\scripts\build_all.ps1
```

流程：静态门禁 → Mock 测试 → 双板型编译 → merge_bin → SHA256 → manifest → zip 总包

输出：`build\release\` 目录，含双板型 merged bin、测试报告、用户手册

### 2.5 编译并烧录

```powershell
.\scripts\dev.ps1 flash              # 自动探测串口
.\scripts\dev.ps1 flash -Port COM3   # 指定串口
.\scripts\dev.ps1 flash -NoBuild     # 跳过编译，直接烧录
.\scripts\dev.ps1 flash -Erase       # 先擦除整片 flash
```

### 2.6 串口监视

```powershell
.\scripts\dev.ps1 monitor
.\scripts\dev.ps1 monitor -Port COM3
```

## 三、编译速度优化

### 3.1 ccache（最有效，已启用）

`sdkconfig.defaults` 已启用 `CONFIG_CCACHE_ENABLE=y`。

- 首次编译：无加速（填缓存）
- 后续增量编译：快 50-80%
- 缓存命中条件：源文件内容 + 编译选项不变

验证 ccache 是否生效：

```powershell
# 编译后查看 ccache 统计
ccache -s
```

### 3.2 Windows Defender 排除项

Windows Defender 实时扫描会扫描每个生成的 `.o` 文件，ESP-IDF 有数千个中间产物。

排除以下目录后，首次编译快 20-30%：

```powershell
# 管理员权限运行
Add-MpPreference -ExclusionPath "D:\workspace\bikepower-c"
Add-MpPreference -ExclusionPath "D:\work\workspace\esp32-power\esp-idf"
Add-MpPreference -ExclusionPath "$env:USERPROFILE\.espressif"
```

### 3.3 增量编译（避免 fullclean）

| 场景 | 命令 | 说明 |
|------|------|------|
| 改了源文件 | `.\scripts\dev.ps1 build` | Ninja 自动增量编译，只编译改动文件 |
| 改了 sdkconfig.defaults | `.\scripts\dev.ps1 reconfigure` | 重新生成 CMake 配置 |
| 改了 CMakeLists.txt | `.\scripts\dev.ps1 reconfigure` | 重新生成 CMake 配置 |
| 切换板型 | `.\scripts\dev.ps1 build -Board m5stamp` | 自动清理旧缓存 |
| 完全重置 | `.\scripts\dev.ps1 clean` | `idf.py fullclean`，删除所有缓存 |

**禁止**：每次编译都先 `clean`，这会让 ccache 失效。

### 3.4 Ninja 并行编译

ESP-IDF v5.5+ 默认使用 Ninja 构建系统，自动根据 CPU 核数并行编译。12 核 CPU 上并行度通常为 12，无需手动设置。

查看构建耗时分布：

```powershell
$env:NINJA_SUMMARIZE_BUILD = "1"
.\scripts\dev.ps1 build
```

## 四、编译时间参考

基于 ESP32-C3（RISC-V 单核 160MHz）项目规模（约 50 个源文件）：

| 场景 | Linux 参考时间 | Windows 参考时间 | 说明 |
|------|---------------|-----------------|------|
| 首次全量编译 | 3-5 分钟 | 4-7 分钟 | 填 ccache 缓存 |
| 增量编译（改 1-2 文件） | 30-60 秒 | 40-80 秒 | ccache 命中 |
| 双板型 Release 全流程 | 8-12 分钟 | 10-15 分钟 | 含门禁+编译+打包 |
| reconfigure | 20-30 秒 | 25-40 秒 | 重新生成 CMake |

Windows 比 Linux 慢 20-30%，主要受 Windows Defender 影响。

## 五、常用命令速查

| 操作 | 命令 |
|------|------|
| 环境配置 | `.\scripts\setup_windows.ps1` |
| 编译 | `.\scripts\dev.ps1 build` |
| 编译（指定板型） | `.\scripts\dev.ps1 build -Board m5stamp` |
| 编译+烧录 | `.\scripts\dev.ps1 flash` |
| 串口监视 | `.\scripts\dev.ps1 monitor` |
| 运行 host 单测 | `.\scripts\dev.ps1 test` |
| 运行静态门禁 | `.\scripts\dev.ps1 check` |
| 清理构建 | `.\scripts\dev.ps1 clean` |
| 重新配置 | `.\scripts\dev.ps1 reconfigure` |
| 擦除 flash | `.\scripts\dev.ps1 erase` |
| 完整 Release | `.\scripts\build_all.ps1` |
| 显示帮助 | `.\scripts\dev.ps1 help` |

## 六、排障

### 6.1 "idf.py 不在 PATH 中"

ESP-IDF 环境未激活。解决：

```powershell
. $env:IDF_PATH\export.ps1
```

或用项目脚本自动激活：

```powershell
.\scripts\dev.ps1 build -IdfPath "D:\work\workspace\esp32-power\esp-idf"
```

### 6.2 "ESP-IDF Python virtual environment not found"

ESP-IDF 工具链未安装。解决：

```powershell
cd D:\work\workspace\esp32-power\esp-idf
.\install.ps1 esp32c3
```

### 6.3 "No module named 'esp_idf_monitor'"

Python 环境不匹配。原因：系统 Python 被误用而非 IDF Python venv。

解决：确保通过 `export.ps1` 激活，或用项目脚本（自动选择正确的 Python）。

### 6.4 编译缓慢

检查项：
1. ccache 是否启用：`ccache -s` 查看命中率
2. Defender 是否排除项目目录：`Get-MpPreference | Select ExclusionPath`
3. 是否误用了 `clean` 导致缓存失效
4. 磁盘空间是否充足（至少 2GB 空闲）

### 6.5 串口找不到

```powershell
# 查看所有 COM 口
[System.IO.Ports.SerialPort]::GetPortNames()

# 指定串口编译烧录
.\scripts\dev.ps1 flash -Port COM3
```

### 6.6 build 目录损坏

```powershell
.\scripts\dev.ps1 clean
.\scripts\dev.ps1 build
```

## 七、相关文件

| 文件 | 说明 |
|------|------|
| [scripts/setup_windows.ps1](../../scripts/setup_windows.ps1) | Windows 环境一键配置 |
| [scripts/dev.ps1](../../scripts/dev.ps1) | 统一开发入口 |
| [scripts/build_all.ps1](../../scripts/build_all.ps1) | 完整 Release 构建 |
| [scripts/flash_latest.ps1](../../scripts/flash_latest.ps1) | 编译+烧录 |
| [scripts/idf_common.psm1](../../scripts/idf_common.psm1) | PowerShell 共享模块 |
| [sdkconfig.defaults](../../sdkconfig.defaults) | 默认配置（含 ccache 启用） |
