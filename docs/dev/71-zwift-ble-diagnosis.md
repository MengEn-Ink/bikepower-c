# 71 · 桌面端 Zwift 搜不到 BLE 设备的根因核对（Windows 11 22H2）

> 结论先行：**不是 Windows 平台蓝牙故障，是 Zwift 客户端进程内 BLE 库加载失败（WinError 1114）**。
> 失败发生在 `LoadLibrary` 阶段，早于任何扫描/配对动作，因此 Zwift 配对页里根本没有设备列表。

## 1. 现场证据（最新一次 Zwift 日志）

日志：`D:\app\Zwift\zoffline\storage\1\logfiles\logfile_-1_2026-11-14_154842.txt.gz`
（15:45:10 启动，15:48 结束；`D:\app\Zwift\zoffline\storage` 下没有比它更新的 logfile，17:46 那次重启未产出新日志）

决定性记录：

```
[15:46:21] ERROR: BLE_LOAD_FAIL; context="calling LoadLibrary", library="BleWin10Lib.dll",
           windowsBuild=22621, windowsBuildMin=15063, errorCode=1114
[15:46:21] WARN : Falling back to default BLE DLL: BleWin10Lib_fallback.dll
[15:46:21] ERROR: BLE_LOAD_FAIL; context="calling LoadLibrary", library="BleWin10Lib_fallback.dll",
           windowsBuild=22621, windowsBuildMin=15063, errorCode=1114
[15:46:21] Could not load the Windows BLE DLL BleWin10Lib_fallback.dll due to the following error 1114
[15:46:21] INFO LEVEL: [BLE] ScanManager: Native BLE unavailable, deferring scan
```

主库与 fallback 库**双双** 1114，随后 `Native BLE unavailable`。此后从 15:46:09 起持续刷屏的都是同一个根因的下游表现：

```
[BLE] BLEModule: Called function 'BLEModule::HasBLE' / 'IsScanning' / 'EnableDeviceDiscovery'
      before BLE has been initialized
[Connectivity] prefsId=414752576  id=0x00000000  roles=[Controllable Trainer]  status=not ready
```

进程侧核对：`ZwiftApp`（PID 24068）当前运行，`(Get-Process ZwiftApp).Modules` 过滤 `Ble|Blue|Win10` **无任何命中** —— BLE 模块从未装载。

## 2. 1114 的触发机制（本机独立进程复现）

脚本：`tests\logs\_probe_zwift_dll.py`（对 `D:\app\Zwift\BleWin10Lib.dll` 与 `_fallback.dll` 各跑三种 COM 套间状态）

| DLL | 线程 COM 状态 | 结果 |
|---|---|---|
| BleWin10Lib.dll | 未初始化 | Load OK |
| BleWin10Lib.dll | STA（`CoInitializeEx(NULL, 0)`） | Load OK |
| BleWin10Lib.dll | **MTA（`CoInitializeEx(NULL, 2)`）** | **FAIL err=1114** |
| BleWin10Lib_fallback.dll | 未初始化 | Load OK |
| BleWin10Lib_fallback.dll | STA | Load OK |
| BleWin10Lib_fallback.dll | **MTA** | **FAIL err=1114** |

即：这两个 DLL 的 `DllMain` 依赖 STA 套间；当加载线程已经把 COM 初始化成 MTA 时，`DllMain` 内的套间初始化返回 `RPC_E_CHANGED_MODE`，Windows 把"初始化例程失败"包装成 `WinError 1114`。Zwift 当前版本是在一个 MTA 线程上加载 BLE 库的，所以两个库都必然失败。

## 3. 排除「Windows 平台蓝牙故障」

同一时刻、同一台机器实测：

- 服务：`bthserv` Running、`BthAvctpSvc` Running（`BluetoothUserService` 按需启动，Stopped 属正常）
- 设备：`Get-PnpDevice -PresentOnly -Class Bluetooth` 多项 State OK（耳机、LE 通用属性服务、键鼠）
- 栈能力：bleak 扫描 8 秒得到 **7 台设备**，命中目标板 `10:20:BA:CC:3A:EE` / `BP_3AEE`（rssi -51）
- 历史证据：Windows 原生 BLE 的扫描、连接、GATT 读写、CPS 通知在 `soak_5min` / `ble_reconnect` 中反复通过

也就是说 Windows 的 BLE 中央栈完全可用；Zwift 侧是在"用不用得上"这一层就断了，问题在客户端 DLL 与进程套间模型，不在平台。

## 4. 两次会话一致 + 注入模块已排除

- 17:46 那次会话（日志 `logfile_-1_2026-11-14_175721.txt.gz`，5388 行）在 `[17:46:47]` 出现与 15:48 完全相同的三条：两个 DLL 均 `errorCode=1114`，随后 `Native BLE unavailable, deferring scan`。说明不是偶发。
- `ZwiftApp` 内确实被注入了 `aiep_sbox.dll` / `aiep_ipc.dll`（`D:\app\DoubaoWork\app\sbox\x64`），但**注入不是本案原因**：独立进程里先 `LoadLibrary(aiep_sbox.dll)` 成功、再 `LoadLibrary(BleWin10Lib.dll)` 仍然成功（脚本 `tests\logs\_probe_inject_then_ble.py`）。另外用 explorer 外壳与计划任务拉起 Zwift，注入依旧存在、1114 依旧出现，与是否经代理进程启动无关。
- 结论收敛：MTA 套间由 **Zwift 自身**在加载 BLE 库的线程上建立，不是本机第三方注入造成，也不是 Windows 蓝牙栈故障。

## 5. 可落地的处置路径（按成本排序）

1. **先排除外部注入**：关闭 Discord/RTSS/手柄与键鼠厂商套件/杀软的游戏内注入，或干净启动（仅 Microsoft 服务）后重试 Zwift。任何先把进程 COM 设成 MTA 的注入模块都会放大这个问题。
2. **重装 VC++ 运行库（x86 + x64）**：Zwift 官方论坛对 `BleWin10Lib.dll` 加载类故障（error 126）给出的标准修复；成本低，可先做。
3. **替换为 Zwift 官方论坛提供的旧版 `BleWin10Lib.dll`**：论坛 2021 年 BLE 故障帖给出的第二套方案。仅从 Zwift 官方渠道取文件，替换前备份现有 DLL。
4. **绕过 Win32 BLE 库**：手机装 Zwift Companion，与 PC 同网，用 Companion 把手机的 BLE 传感器桥给 PC。这条路不依赖 Zwift 的 Win32 BLE 库，是目前最稳的可用方案；注意手机端另有一个"连上但数值 0"的问题待解。
5. **ANT+ 路径**：Zwift PC 原生支持 ANT+，但本固件只做 BLE，需要额外 BLE→ANT+ 桥接硬件，优先级最低。

## 6. 边界

- 未修改 `D:\app\Zwift` 下任何文件；上述第 3 条需用户确认来源后再动。
- 未做干净启动验证（需要改系统服务与重启），属人工步骤。
- 17:46 那次重启没有产出新日志文件，本结论基于 15:48 那次的完整日志与本机复现脚本，两者互相印证。
