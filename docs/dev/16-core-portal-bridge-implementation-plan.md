# 核心门户与真实功率桥接实施计划

状态：用户已确认第二版原型并授权实施、功能测试及固件名称精简。

交付约束：用户已授权提交与合并代码；版本候选固件须用户手动验证并明确批准后才能发布。当前测试包仍为基线 Git `f498429` 下构建的工作区候选，提交后的新 Git ID 不回填到既有固件证据。

目标：将 `15-core-portal-rewrite-design.md` 和第二版离线原型落到 ESP32-C3，保持模拟、跑步、Pages 配置能力，增加真实功率及同源踏频桥接。

架构：纯 C 负责 CPS 解码、曲柄事件、校正和失效保护；固定静态 BLE 会话负责上游连接、DIS 读取、订阅及限时重连；应用主循环负责配置落盘、来源切换和统一输出。HTTP 仅提交命令，不在请求栈或 NimBLE 回调中执行持久化或等待连接。

技术栈：C11、ESP-IDF 5.5.4、NimBLE、FreeRTOS、原生 HTML/CSS/JS。

## 已核对的基线

- 当前固件版本 `4.2.1`，schema 已为 v12；桥接升级到 **v13**，保留历史 v11→v12 功率浮动迁移。
- `make -C test run`：6144 通过，0 失败。
- `python3 tests/test_ble_protocol_vectors.py`：3 项通过。
- 用户未提交文件仅为本会话原型及设计文档；实施前无生产代码修改。
- 本机检测到 `/dev/cu.usbmodem2101`，烧录前重新核对设备和串口占用。
- 按当前用户授权顺序实施和验证；不因计划完成再次请求执行许可。

## 批次一：领域、配置及协议

文件：

- 新增 `components/bikepower_domain/cps_input_codec.c` 及 `include/cps_input_codec.h`：完整 CPS flags 和可选字段长度检查。
- 新增 `components/bikepower_domain/power_bridge.c` 及 `include/power_bridge.h`：纯 C 配置、曲柄差分、有效样本门槛和过期保护。
- 修改 `power_engine.c/.h`、`config_store.c/.h`、`domain_config.h`、`ble_config_codec.c/.h`。
- 修改 `main/nvs_store.c`、`main/bikepower_config.h`、两处 CMake 和 `test/Makefile`。
- 新增独立桥接领域测试，更新人工协议向量及配置一致性门禁。

- [x] 先写失败测试：可选字段前置偏移、截断、负功率、0W、曲柄回绕、重复、时间差非法、无曲柄设备。
- [x] 实现两份有效通知才有效、3 秒失效、明确断线立即失效；曲柄停更不得泄漏旧踏频。
- [x] 测试偏移和比例整数舍入、0W 保护、0–2000W 夹紧（先校正再夹紧，2500W × 50% = 1250W）。
- [x] 引擎独立保存 `bridge_config_t`，桥接只使用原始功率驱动可选模拟心率；不得改 `base_power` 或模拟踏频。
- [x] schema v13 补 bridge 默认值；v12→v13 默认模拟、无绑定、原值，不自动扫描或连接。
- [x] BLE 配置追加 bridge 组，不改旧组布局；47 字节以内包含来源、绑定、地址类型、地址、校正和显示名。
- [x] NVS 存储显式编码的 bridge blob，禁止持久化 C struct padding。
- [x] 运行领域与协议测试：host 6146/0 + 桥接独立 suite，13 个协议固定向量，配置一致性 279/0，ESP-IDF 编译通过，app 剩余 72%。

bridge 配置线格式：

```text
version(1), source(1), bound(1), addr_type(1), addr(6),
calibration_mode(1), watts(int16 LE), percent_x10(int16 LE),
name_length(1), name(0..31 UTF-8 bytes)
```

## 批次二：上游会话、统一输出及配置事务

文件：

- 重写 `main/ble_scanner.c/.h` 为有界 CPS 候选扫描，不依赖模板库。
- 新增 `main/ble_power_bridge.c/.h`，静态上下文及主循环 tick；回调仅解析或更新受保护的小快照。
- 修改 `main/app_main.c`、`main/ble_bikepower.c/.h`、`main/radio_ctrl.c`。
- 删除模板库及分析模块前先迁移所需 DIS 字段读取、测试和构建依赖。

- [x] 主动限时扫描，无名称 CPS 可选；以候选编号操作，不按名字绑定。
- [x] 发现 CPS Measurement 与 CCCD，确认订阅写入结果；读取 GAP 名称和 DIS 六字段。
- [x] 测试最长 30 秒，不改来源或 NVS；更换前释放旧上游连接。
- [x] 会话等待、连接、订阅、有效、过期、停止及错误各有明确状态。
- [x] 10 秒没有有效通知主动断开；60 秒重连窗口按 1、2、4、8 秒退避，之后保持 8 秒。
- [x] WiFi 关闭结束扫描/临时测试，已运行桥接继续；radio 重建或失败兜底先释放上游。
- [x] CPS 无曲柄时输出 4 字节功率包；CSC 仅在真实曲柄有效时通知；桥接 FTMS 无模拟速度，flags 与功率、踏频、可选心率一致。
- [x] 桥接拒绝 ERG 目标，不偷偷切回模拟；下游连接计数不包含上游。
- [x] 配置带 revision，应用与持久化在主循环完成；HTTP 保存失败不改变运行状态，结果不明可查询核实。
- [x] 复制身份使用已有 `bp_ident`；只复制有效读取字段，名称按用户选择，缺失字段保留原覆盖或内置值。
- [ ] 下游双次重连、骑行/跑步通知回归已通过；上游真实桥接连接组合仍未覆盖。

本批中间验证：生产 `ble_power_bridge.c` 与 `ble_scanner.c` 使用时钟/NimBLE 替身在宿主机运行，ASan/UBSan 覆盖七字段读取、CCCD 确认、两样本、3 秒失效、10 秒断开、首次重连、代次隔离、取消、门户关闭和 CPS 候选筛选。host 6146/0、协议向量、WiFi 完成契约及离线双次重连通过；ESP-IDF 构建 app `0x1189f0`，剩余 72%。尚未烧录，前端与 mock 仍待替换，不能作为端到端通过证据。

## 批次三：设备门户与产物名称

文件：

- 重写 `main/web/index.html`、`app.css`、`app.js`，参考 `mobile-prototype-v2.html`。
- 修改 `main/wifi_portal.c/.h`、`tests/mock_server.py`、门户静态及交互测试。
- 修改 `scripts/inline_static.py` 和资源预算门禁。
- 修改 `main/board_detect.c`、双平台 build_all、产物检查器、Pages 固件准备脚本及其测试。

- [x] 默认模拟；选择真实后渐进披露；实时输出、草稿和配置应用严格分离。
- [x] 仅明主题，按钮居中，44px 点击区、16px 输入；轻动画遵守 reduced-motion。
- [x] 固定功率 / 机器人共用配置区，自定义位于机器人档；实际范围与领域常量一致。
- [x] 保留模拟与跑步浮动、锁定/联动心率和已有预设，移除模板和 LED 门户。
- [x] 首次功率计测试草稿心率默认关闭，用户可开启，更换不覆盖手动选择。
- [x] 新 `/api/runtime`、`/api/bridge/scan`、`/api/bridge/control`；删除旧模板与分析接口。
- [x] 配网默认 300 秒，只有用户续期重置；轮询、扫描和测试不自动续期。
- [x] 内联总量硬门禁 110 KiB，约 70.4 KiB，分项均通过。
- [x] 文件名 `<板型>-v<版本>.bin`，总包 `bikepower-c-v<版本>.zip`；Git ID/构建信息保留在 manifest，页面显示短版本和独立详情。Pages 支持现有历史 Release 长文件名。
- [x] 压缩生产页面手机/桌面 mock 交互、保存失败、过期、身份复制和输入焦点回归；动态文本使用 textContent。

## 最终验证和文档

- [x] 全量 host、配置一致性、门户结构/JS、协议向量及 quality/operation gate。
- [x] generic-c3 / m5stamp-c3 全量构建，合并单文件及 SHA256/manifest；修复栈溢出后已重新构建，显式关闭自动配网测试开关。
- [x] 当前实物烧录、串口启动、BLE 服务、六组配置事务、NVS 重启及两轮重连。
- [ ] 上游功率计 + 下游 + WiFi 并行验证，堆始终 >80 KiB；若没有真实功率计，使用独立 BLE 上游测试夹具，并明确真实硬件未覆盖。
- [x] 更新 README、CHANGELOG、PROGRESS、用户手册、Wiki、BLE/WiFi 规则及随固件报告。
- [x] 报告逐项记录真实结果；当前 generic 自动门禁及 302.2 秒耐久通过，配网 HTTP/真实桥接、M5 实物、Safari、物理按钮/LED/骑行 App 明确标记未覆盖，未写成通过。

## 本轮补充：无前缀命名与 Pages 配置同步

- [x] 先在 `tests/test_prepare_web_flash_site.py`、`tests/test_release_artifacts.py` 使用无前缀板型附件，运行两个测试确认旧实现失败；历史附件测试保留。
- [x] 在 `tests/check_ble_monitor.py` 加入校正编码、原绑定保留、未绑定拒绝真实来源、旧固件表单降级和事务不覆盖草稿的运行时断言，先运行确认失败。
- [x] 修改 `board_detect.c`、双平台 `build_all`、产物校验和手册生成器，统一 `generic-c3-v4.2.1.bin` / `m5stamp-c3-v4.2.1.bin`；Pages 正则以可选 `bikepower-c-` 兼容历史。
- [x] Pages 新增来源、原值/偏移/比例校正表单，保留地址类型、地址和设备名原始字节；缺少 Bridge 特征时只保存原五组。无绑定禁止切真实，首次扫描绑定从设备配网页完成。
- [x] 运行 `python3 tests/check_ble_monitor.py`、两份附件回归和浏览器浅暗主题验证；重建双板，generic 保留 NVS 烧录并核对 DIS 名称与六组配置。
- [x] 更新本轮报告和分发包 SHA256；记录本地 Pages 已更新、线上尚未部署。未经授权不提交或推送。
