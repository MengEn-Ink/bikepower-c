# 本地虚拟开发板：运行与验收边界

## 结论与范围

BikePower 主线采用 ESP Emulator 0.51.0 + Bumble 0.0.219，运行实际 ESP32-C3 ROM、bootloader、应用和固件里的 NimBLE Host，不把业务逻辑重写成 mock。日常先在 Windows/macOS 本地验证，CI 运行同源回归；真实开发板只承担模拟器不能证明的验收。

- Windows 已实测：原生 ESP-IDF 5.5.4 构建，同机 WSL2 Ubuntu 运行模拟器、虚拟 UART/HCI 和纯软件客户端。无需 Docker、真实串口或蓝牙适配器。
- macOS Apple Silicon：固定版原生包及 shell 入口已提供，当前没有 macOS 机器，未实机验证。
- Intel Mac：固定版没有原生包；WASM/容器只是未验证备选，不保证兼容。
- Wokwi/QEMU 不承担项目 BLE 主线；公开能力调研见文末一手来源。

## Windows 入口

已安装 WSL2、Python 3.9–3.12 和 Python venv 支持后，在仓库根目录运行：

```powershell
./scripts/virtual_board.ps1 setup
./scripts/virtual_board.ps1 doctor
./scripts/virtual_board.ps1 build -IdfPath <ESP-IDF-5.5.4目录>
./scripts/virtual_board.ps1 test -Security -Running
./scripts/virtual_board.ps1 debug -Gdb gdb-multiarch
```

`-Distribution` 可指定现有 WSL 分发版。构建 Python 优先使用 `-IdfPython`，其次 `IDF_PYTHON_ENV_PATH`，最后唯一的 `.espressif/python_env/idf5.5_*`。多个候选必须明确指定，不能猜测。IDF 路径优先显式参数或有效 `IDF_PATH`，最后项目 `build/toolchain/esp-idf-v5.5.4`。

`debug` 默认使用 WSL 内 `gdb-multiarch`，可在 WSL 安装该包；不自动修改 Windows 防火墙或系统网络。Windows 原生交叉 GDB 也已实测可直连同机 WSL 地址：localhost 连接曾失败，使用 WSL 实际地址后命中 `app_main`、读取栈和单步成功。自动化入口把客户端放在 WSL 内，避免该转发差异。

`-BuildDir` 可指定独立构建目录。`-SdkconfigDefaults` 支持额外构建 defaults，缺文件直接失败；根 `sdkconfig` 不改。入口始终固定 generic-c3 和 `PORTAL_TEST_AUTO_CONFIG=OFF`，不刷真实设备。旧已提交产物可以指定 `-BuildDir build/final-build-4.3.3`，但预期 Git 身份仍须与那个构建一致。

## macOS Apple Silicon / Linux 入口

```sh
sh scripts/virtual_board.sh setup
sh scripts/virtual_board.sh doctor
# 先按ESP-IDF官方步骤激活5.5.4环境
sh scripts/virtual_board.sh build
sh scripts/virtual_board.sh test build/virtual-board-build --security --running
sh scripts/virtual_board.sh debug build/virtual-board-build --gdb riscv32-esp-elf-gdb
```

shell 入口接受相对或绝对 build 路径，GDB 可指定 `--gdb`。Linux/WSL 上已验证运行路径；macOS 包安装、构建、BLE 与 GDB 均须补本机实测，不能据 shell 语法通过宣布 macOS 全部通过。

## 锁定与隔离

- `scripts/tools/virtual_board_lock.json` 固定模拟器版本、三平台 archive SHA256、IDF/Bumble/esptool 核心版本。
- `requirements-virtual-board.txt` 锁定本轮实际 Python 运行依赖；未包含跨平台 wheel 哈希，不宣称供应链字节级可复现。
- 模拟器安装只从校验过的 archive 提取唯一普通可执行文件，不解压任意目录，不自动 self-update。
- 运行和虚拟刷写分别复制输入镜像，`--save-state` 只用于私有副本；输入 SHA256 事后检查。
- 各运行保存独立报告；虚拟 keys 与运行 Flash 副本不提交、不公开打包。CI 仅上传 report JSON/Markdown 与 GDB 证据，不上传整个会话目录。
- 原始 firmware/ELF 哈希区分构建；固件内嵌 Git 短 SHA 不含 dirty 状态，不能单独证明“干净提交固件”。

## 已执行的验证

本轮 Windows dirty 候选完整回归曾耗时 50.062 秒：4 KiB 虚拟刷写/verify/精确回读，真实 ROM/ELF/Git 身份，八组配置、骑行四路通知、事务错误拒绝、普通重连、NVS reset 保持、GATT 重建、原配置恢复、SC 配对/checked LTK、ERG 320W Start/Stop、保留绑定复连、跑步距离增长与恢复。辅助收尾加固后再跑通过，并增加原配置重启回读。

已提交的 4ecf069 基线也独立执行完整安全+跑步回归通过；不把 dirty 候选 PASS 转移给它。最终工具提交对应的新源码构建、远端 CI 状态另见本轮交付报告。

GDB 自动化入口已实跑通过：`app_main` 断点、调用栈、PC/SP 和单指令步进，单次约 1.831 秒（不含构建/安装）。没有据此宣称真实 JTAG 调试通过。

异常和取消在配置修改后分别注入，均保留整体 FAIL/非零退出，收尾提交原完整快照、回读、reset 后再次回读 PASS。测试注入的 FAIL 是正确的拒绝结果，不记普通 E2E PASS，也不消除以前失败的原报告。

虚拟 UART 门禁只写 `0x10000` 起 4096 字节；完整 4 MiB 合并镜像用于真实固件运行。未实施整片 4 MiB 重新刷写，不能扩大为完整 Flash 刷读验证。

## 安全模型和不可替代的验收

Bumble 默认 LocalLink 的 encryption 路径不请求 peripheral LTK。本项目测试专用 CheckedController 在 central enable encryption 后发送 LTK Request，双方 16 字节密钥一致才发送成功；缺失、失配、重复 reply、未知 handle、重置残留和未加密 key-size 均需拒绝。它是 Host/HCI 协议模型，不模拟真实射频 AES-CCM。

未实现的 Privacy Mode/RSSI 命令保留 Unknown Command 并写入报告，不能通过假 success 掩盖模拟器不足。固定公共虚拟客户端身份只解决 LocalLink 路由与 Host 身份一致，不证明 RPA/隐私地址解析。

以下仍需真机：Windows/手机 GATT 缓存、跨表已绑定客户端的 Service Changed 订阅落盘/发送/确认、真实射频加密、WiFi/BLE 共存、真实功率桥接、实体断电/电气/按钮/灯效、目标骑跑 App、长时控制耐久。原 v4.3.3 发行阻断保持；不创建 tag/Release，不因本地仿真通过放行。

## 提效方式

开发循环为离线单测 → 隔离增量构建 → 本地完整虚拟回归 → 必要真机专项。模拟器可创建独立 Flash/NVS 客户端状态，不占用唯一开发板；报告和失败退出适合自动回归。已测约 50 秒的是单次热环境回归，不含冷安装、固件构建或 CI 排队，未做真机对照测速，不宣称固定倍数/百分比提速。

## 一手来源

- https://github.com/espressif/esp-emulator
- https://github.com/espressif/esp-emulator/releases/tag/v0.51.0
- https://github.com/espressif/esp-emulator/blob/main/docs/guides/BLE.md
- https://github.com/espressif/esp-emulator/blob/main/docs/guides/FLASHING.md
- https://github.com/google/bumble
- https://docs.wokwi.com/guides/esp32
- https://github.com/espressif/qemu

main 文档可能晚于固定 v0.51.0；运行参数以固定二进制 `--help` 和本轮实际结果为准，不能用 main 新功能承诺固定版支持。
