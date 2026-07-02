# 更新日志

本项目遵循 [SemVer](https://semver.org/lang/zh-CN/)。版本号格式 `vMAJOR.MINOR.PATCH`。

## [未发布]

### BLE 设备信息模板库
- `main/ble_scanner.c/.h`：新增模板库专用 BLE Central/Observer 能力，支持用户触发的限时扫描、扫描结果缓存、临时连接外部设备并读取 GAP Device Name 与 DIS 厂商/型号/固件/硬件/软件字段，完成后主动断开。
- `main/wifi_portal.c`：接入 `/api/ble-scan/start|stop|results|read`，扫描结果和读取结果以 JSON 返回给前端；模板保存仍走独立 `ble_tpl` NVS namespace。
- `main/web/`：模板库扫描按钮接入真实扫描，前端轮询扫描结果，点击设备后读取基础信息并要求确认别名再保存；保留应用模板与二次确认删除交互。
- `main/ble_scanner.c`、`main/web/app.js`：扫描结果排除未命名 BLE 设备，只展示带 alias/device_name/adv_name 的设备，避免用户误点不可识别广播。
- `components/bikepower_domain/device_template.c/.h`：新增 BLE 地址格式化和标准服务 UUID 到模板服务标记的映射辅助函数。
- `sdkconfig.defaults` / `sdkconfig`：启用 NimBLE Central/Observer 角色，并把连接数提升到 2，为“本设备作为 Peripheral + 临时连接外部设备”预留连接资源。
- 验证：`make -C test && test/test_domain` 1364 通过 0 失败；`python3 tests/check_portal_js.py`、`python3 tests/check_portal_structure.py`、`python3 tests/check_config_consistency.py`、`python3 tests/check_nvs_schema.py`、`python3 -m py_compile tests/mock_server.py` 通过；`idf.py build` 通过，app 分区剩余 73%；mock 本地服务返回 3 个扫描项（含 1 个未命名），网页端仅展示 2 个有名称设备。

### Captive Portal 自动弹窗
- `main/wifi_portal.c`：注册 HTTP 404 兜底 handler，未知系统探测路径统一重定向到首页，覆盖 Chrome/Chromium 等新路径和系统探测路径变化，避免只注册固定 10 个 URI 导致多次连接后不弹窗。
- `main/captive_portal.c`：探测重定向改为 `303 See Other` + `Location: /` + 短 body，参考 ESP-IDF captive portal 示例补强 iOS Captive Network Assistant 对“仅空 302”的兼容性；停止 DNS 时先 `shutdown()` 再 `close()`，更稳定唤醒阻塞的 `recvfrom()`。
- `main/radio_ctrl.c`：DHCP Option 114 的 Portal URI 改为静态存储期字符串，满足 ESP-IDF 要求“应用需在 DHCP Server 生命周期内保持 URI 指针有效”。
- `tests/stage5_portal_test.py` / `tests/mock_server.py`：新增已知探测端点和未知探测路径兜底重定向验证；mock server 同步 303 + body 行为。
- 验证：`python3 tests/stage5_portal_test.py --ip 127.0.0.1 --port 18081` 在修复前 5/7 失败于空 body 与未知路径 404，修复后 7/7 通过。

### 日志体系
- `tests/serial_daemon.py`：新增设备重启 banner 识别，默认插入 `DEVICE REBOOT / BOOT BANNER` 分隔线；新增 `--split-on-reboot` 支持按重启切新 `device_*.log`；新增 panic/WDT/Backtrace/Guru Meditation 等关键字故障快照 `crash_*.log`，并支持 `--no-crash-snapshot` 关闭。
- `tests/test_serial_daemon.py`：新增串口日志守护单测，覆盖重启分隔线、按重启切文件、故障快照生成。
- `scripts/flash_latest.sh`：修复 `set -u` 下 `PORTAL_TEST_AUTO_CONFIG=$TEST_AUTO_CONFIG` 变量名边界解析问题，确保烧录脚本可完成编译、烧录和启动日志抓取。
- `.trae/rules/coding-style.md`、`docs/dev/01-technical-reference.md`、`tests/README.md`：补充固件端日志级别语义、TAG/限频/禁区、主机端日志命名、重启切分、删除保留和故障证据规则。
- 验证：`python3 -m unittest tests/test_serial_daemon.py`、`python3 -m py_compile tests/serial_daemon.py tests/test_serial_daemon.py`、`bash -n scripts/log_daemon.sh scripts/flash_latest.sh`、`git diff --check`、`bash scripts/flash_latest.sh` 通过；真机启动日志 `tests/logs/flash_latest_20260701_204055.log` 无 ERROR/WARN/panic/WDT/Backtrace。

### 网页端
- `main/web/`：恢复设备名称“文本展示 → 右侧编辑 → 对勾确认”的交互，不再默认展示为直接输入框；确认后随“保存设置”持久化。
- `main/web/`：物理按钮和 LED 指示灯配置改为默认收起的折叠卡片，点击卡片标题才展开编辑，减少设置页首屏信息密度。
- `main/web/app.css`：优化设置页视觉层次，增加浅蓝渐变头部、设备名称信息卡、折叠标题栏和暗色模式适配。
- `tests/check_portal_structure.py`：新增设备名称确认交互与折叠卡片默认收起的静态契约检查。
- `main/web/`：修复自定义配速机器人 W/kg 修改后下拉框仍显示旧区间的问题；新增自定义机器人昵称输入，保存后下拉框显示新昵称与当前 W/kg 范围。
- `components/bikepower_domain/`、`main/nvs_store.c`、`main/wifi_portal.c`：新增 `custom_pacer_name` 配置字段，贯通默认值、校验、序列化、NVS 持久化与 `/api/state` / `/api/config`。
- `tests/`：补充 host 单测、门户结构检查、NVS schema 检查、mock server 与配网自测对自定义昵称/区间契约的覆盖。
- 验证：`test/test_domain` 1322 通过 0 失败；`check_portal_js.py`、`check_portal_structure.py`、`check_nvs_schema.py`、`check_config_consistency.py`、`check_tasks_wdt.py`、`check_board_desc.py` 全绿；`stage5_portal_test.py --ip 127.0.0.1 --port 18080` mock 自测 5/5 通过；`flash_latest.sh --capture-seconds 20` 编译/烧录通过，启动日志 `tests/logs/flash_latest_20260701_221449.log` 无 panic/WDT/Backtrace；`ble_selftest.py` 通过。
- `main/web/`：底部导航由“首页 / 更多 / 关于”重构为“首页 / 设置”两 Tab；原“更多”和“关于”并入设置页，设置页内分为“用户资料 / 设备信息 / 关于”三级内容入口。
- 设备名称编辑从关于页挪入“设置 → 设备信息”，与固件版本同组展示；“用户资料”保留体重、FTP、阈值心率和物理按钮调功步进。
- 首页进一步压缩卡片、输入、步进器和分段控件高度，减少固定模式首屏滚动压力。
- `tests/check_portal_structure.py` 增加前端信息架构静态检查，锁定 2 Tab 和设置页三分区结构。

### 新增（自定义配速档位）
- 在 5 个预设配速机器人（coco/yumi/jacques/genie/constance）外新增第 6 档「自定义」（id=custom，index=5）：用户可自填 W/kg 下限+上限（1.0~7.0，每档 0.1），road 模式按 `weight × 自定义W/kg` 输出功率。
- `components/bikepower_domain/`：`domain_config.h` 新增 `PACER_INDEX_CUSTOM/RIDE_TYPE_COUNT/CUSTOM_WKG_*`；`profile_t` 增 `custom_wkg_low/high` 字段与 `profile_set_custom_wkg`/`profile_effective_wkg`；`profile_ride_type_index`/`set_ride_type_str` 循环改 `RIDE_TYPE_COUNT`（含 custom）；`build_road`/`reset_mode_state` 改用 `profile_effective_wkg`；`get_current_pacer` 自定义档返回 "Custom"。
- `config_store.c/.h`：`config_data_t` 增 `custom_wkg_low/high`，defaults/validate（夹紧 + low≤high）/serialize（栈缓冲 352→416）/deserialize 同步。
- `main/nvs_store.c` + `main/bikepower_config.h`：新增 `NVS_KEY_CUSTOM_WKG_LO/HI`（"cwkglo"/"cwkghi"）与双向映射。
- `main/wifi_portal.c`：`/api/state` 顶层输出 `custom_wkg_low/high`，pacers 数组追加 custom 档；`apply_json_fields` 先于 ride_type 解析自定义范围。
- `main/web/`：road 页配速档位下拉新增「自定义」，选中后展开「W/kg 下限/上限 ±[−][+]」步进器（0.1 一档），实时显示功体比与预计功率区间。
- `tests/mock_server.py` 同步 custom 档与字段；`test/test_domain.c` 新增自定义档切换/功率范围/越界夹紧断言（host 单测 1284 通过）。

### 新增（调功步进可配置，可配网调整并持久化）
- `components/bikepower_domain/`：新增 `power_step` 引擎字段与 `power_engine_set_power_step` setter（越界夹紧 `[POWER_STEP_MIN, POWER_STEP_MAX]` = 5~50W，默认 20W）；`domain_config.h` 新增 `POWER_STEP_MIN/MAX/DEFAULT`；`config_store` 三处（defaults/validate/serialize/deserialize）同步，serialize 栈缓冲 320→352。
- `main/app_state.c`：物理按钮短按 −步进、长按连续 +步进 改用 `s_engine->power_step`（原硬编码 `POWER_STEP_W`）。
- `main/nvs_store.c` + `main/bikepower_config.h`：新增 `NVS_KEY_POWER_STEP`（"pstep"）与 engine↔config 双向映射。
- `main/wifi_portal.c`：`/api/state` 输出 `power_step`；`apply_json_fields` 解析 `power_step`（任意模式接受、setter 夹紧）。
- `main/web/`：「更多」页新增「物理按钮 · 调功步进 ±[−][+]」步进器（5W 一档，5~50W）。
- `tests/mock_server.py` 同步 `power_step` 字段与 `/api/config` 接受；`test/test_domain.c` 新增调功步进 setter 边界与 config 往返断言（host 单测 1177 通过）。

### 修复
- 修复配速机器人（road 模式）在网页端完全不生效的缺陷：此前「更多」页的机器人下拉框只写 `profile_ride_type`，而该字段仅在 `mode==road` 时被 `build_road` 使用，但前端无任何切换 `mode` 的入口（`STATE.mode` 恒为 `/api/state` 回显值），导致选择任何档位都不改变输出。现首页顶部新增「固定模式 / 配速机器人」分段控件，真正驱动引擎 `mode`。

### 新增（固定模式浮动幅度，可配网调整并持久化）
- `components/bikepower_domain/power_engine.c/.h`：新增 `power_float` / `cadence_float` / `hr_float` 三个浮动幅度字段与 `power_engine_set_power_float/set_cadence_float/set_hr_float` setter；新增 `fluct_walk_t` 有界平滑随机游走状态。
- `build_steady` 改用有界平滑随机游走替代逐拍随机：每 `FLUCT_WALK_HOLD_LOOPS` 拍在 `[-amp,+amp]` 带内重采样目标，再一阶指数逼近（`FLUCT_WALK_TAU`），输出恒在 `[基准−浮动, 基准+浮动]` 内平稳变化，`amp=0` 时恒定。
- `hr_model_steady_user` 增加 `amp` 参数，固定心率按浮动幅度平稳波动，`amp=0` 恒定。
- `components/bikepower_domain/include/domain_config.h`：新增 `POWER_FLOAT_*` / `CADENCE_FLOAT_*` / `HR_FLOAT_*` 范围与默认、`FLUCT_WALK_TAU/HOLD_LOOPS`，移除旧 `STEADY_USER_HR_FLUCT_LO/HI`。
- `config_store.c/.h`：`config_data_t` 增加三浮动字段，`defaults/validate/serialize/deserialize` 同步（serialize 栈缓冲 256→320）；`CONFIG_VERSION` 保持 5（additive，旧配置缺键回落默认不重置）。
- `main/nvs_store.c` + `main/bikepower_config.h`：新增 `NVS_KEY_POWER_FLOAT/CADENCE_FLOAT/HR_FLOAT`（均 ≤15 字符）与 engine↔config 双向映射。

### 网页端
- `main/wifi_portal.c`：`/api/state` 输出 `power_float/cadence_float/hr_float`；`apply_json_fields` 解析三浮动字段（任意模式接受、setter 内夹紧）。
- `main/web/index.html + app.css + app.js`：首页「固定/机器人」分段切换；每卡片内联「浮动 ± N [−][+]」步进器（心率浮动仅锁定心率时显示）；road 模式显示配速档位选择 + 实时功体比/预计功率区间；ERG 模式显示只读提示。
- 关于页重写为 4 页：设备信息+三种模式 / 功率→心率关联 / 算法逻辑说明 / 使用与注意；内容填充满屏，分页圆点固定在底部导航栏上方。
- `tests/mock_server.py`：同步三浮动字段、`hr_locked`、真实五档位机器人，`/api/config` 接受 `mode/hr_lock/*_float`。

### 测试
- `test/test_domain.c`：新增 `test_float_setters`，更新 `hr_model_steady_user` 新签名调用、steady 快照浮动断言、config 往返浮动字段断言。host 单测 1265 通过 0 失败；门户结构/JS/配置一致性静态门禁全绿。

## [4.0.0] - 2026-06-30

### 移除（不向后兼容）
- 移除在线 OTA 功能：删除 `main/ota_downloader.c/.h`、`main/ota_update.c/.h` 及领域层 `ota_version.c/.h`、`ota_selftest.c/.h`。
- 移除家庭网络配置：`/api/wifi/*`（扫描/选网/测试连接/保存凭证）一并删除（其唯一用途是为 OTA 提供 STA 凭证）。
- `main/wifi_portal.c` 删除 `/api/ota/check|status|upgrade`、`/api/wifi/*` 路由与异步任务，及死路由 `/api/draft`。
- `main/radio_ctrl.c/.h` 删除 STA 相关全部代码（`radio_ctrl_sta_connect/disconnect`、`radio_ctrl_enter_sta_exclusive`、`radio_ctrl_wifi_scan`）。
- `main/nvs_store.c/.h` 删除家庭 WiFi 凭证读写；`main/bikepower_config.h` 删除 OTA 常量与家庭 WiFi namespace/key。
- `main/app_main.c` 删除 OTA 启动自检确认/回滚调用。
- 前端 `main/web/`：删除「升级」「WiFi」视图、对应导航项、`/ota` `/wifi` 路由、`captureDraft` 之外的 draft 提交，及旧版未引用 CSS。

### 分区表与构建
- `partitions.csv` 由双 OTA（`ota_0`/`ota_1`/`otadata`）改为单 `factory` 分区，Flash 仍维持 4MB，app 可用空间从 1.75MB 提升到约 3.875MB。
- `sdkconfig.defaults` 删除 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`；`main/CMakeLists.txt` 删除 `app_update`/`esp_https_ota`/`esp_http_client`/`mbedtls` 组件依赖。
- 固件版本号升至 `v4.0.0`（分区表不兼容，需 `erase_flash` 全量重新烧录，不能走 OTA 平滑迁移）。

### 测试与文档
- 删除 `tests/ota_flow_test.py`、`tests/ota_local_mock.py`、`tests/ota_serial_monitor.py` 与 `docs/user/04-ota-user-guide.md`、`docs/archive/05-ota-implementation-plan.md`。
- 同步清理 `tests/check_config_consistency.py`、`tests/check_portal_structure.py`、`tests/check_nvs_schema.py`、`tests/mock_server.py`、`tests/stage5_portal_test.py`、`scripts/build_all.sh`、`scripts/flash_latest.sh`、`scripts/tools/quality_gate.py`、`test/test_domain.c` 中的 OTA/WiFi 引用。
- host 单测 1127 通过 0 失败；门户结构/JS/配置一致性/NVS schema/任务 WDT/板型静态门禁全绿。
- 方案文档见 [docs/archive/07-remove-ota-plan.md](docs/archive/07-remove-ota-plan.md)。

## [3.2.0] - 2026-06-29

### 重构（配网门户前后端分离）
- `main/wifi_portal.c` 大幅瘦身（2277 → 1076 行）：21 个 `snprintf` 拼 HTML 的 handler 全部改为 15 个 `/api/xxx` JSON 接口，C 侧不再含任何 HTML/CSS/JS 字符串。
- 新增 `main/web/` 目录存放前端三件套：`index.html`（SPA 入口，含 `<!-- INLINE_CSS -->` 与 `<!-- INLINE_JS -->` 占位符）、`app.css`（统一所有视图样式）、`app.js`（SPA 路由 + `XMLHttpRequest` 调用 `/api/xxx` 自行渲染）。
- 新增 `scripts/inline_static.py`：编译时把 `app.css` 与 `app.js` 内联到 `index.html`，生成 `main/web_index_html.h`，C 侧 `GET /` 直接吐出整页 HTML。
  - 采用内联方案而非外部 `.css/.js` 链接：iOS Captive Portal WebView 不加载外部资源，必须单文件。
- 更新 `main/CMakeLists.txt`：`add_custom_command` 调用 `inline_static.py` 生成头文件，依赖 `web/index.html + app.css + app.js`。注意 `add_custom_command` 必须放在 `idf_component_register` 之后，否则 ESP-IDF 在 `component_get_requirements` 阶段报 `command is not scriptable`。

### 路由契约变更（不向后兼容）
- 旧路由 `/save /scan /connect /status /ota /more /about /ui/module /success /closing /draft /time /renew /mem_stats` 全部废弃。
- 新路由统一前缀 `/api/`，POST 接口返回 JSON `{"ok":true}` 或 `{"status":"..."}`，不再返回 302 重定向。
- `GET /` 由「拼首页 HTML」改为「吐内联 HTML 整页」，前端 SPA 通过 `location.hash` 切换视图。

### 测试体系更新
- `tests/check_portal_js.py` 重写：直接对 `main/web/app.js` 跑 `node --check`，不再从 C 字符串提取 `<script>` 块。
- `tests/check_portal_structure.py` 重写：校验 `index.html` 基础结构 + 占位符、`wifi_portal.c` 路由表完整性、`app.js` 前端 API 调用完整性（35 项断言全绿）。
- `tests/stage5_portal_test.py` 重写：适配新 JSON 路由，`/api/draft` 改为 POST JSON 验证 `{"ok":true}`。
- `tests/ota_flow_test.py` 重写：静态部分校验 `app.js` 中 OTA 函数定义与 API 调用一致性，动态部分校验 `/api/ota/check|status|upgrade` 状态机。
- 新增 `tests/mock_server.py`：~250 行 Python 标准库 `http.server` 实现，复刻全部 `/api/xxx` 接口返回假数据，前端可在本地不烧设备调试。Mock 行为：OTA 检查 2s 后转 `done has_update=true`，WiFi 扫描 1.5s 后返回 3 个假 AP，WiFi 连接 2s 后 `connected=true`。

### 文档同步
- `README.md` 目录结构补充 `main/web/` 子目录。
- `docs/04-technical-reference.md` 第 8.3 节路由表更新为 v3.2.0 的 15 个 `/api/xxx` 接口，删除旧 21 路由描述。
- `main/bikepower_config.h` `FIRMWARE_VERSION` 由 `3.1.8` 升至 `3.2.0`。

### 测试报告（本轮审计）
- 静态门禁：`check_portal_js.py`、`check_portal_structure.py`（35 通过 0 失败）、`ota_flow_test.py --static`（ALL PASS）、`check_config_consistency.py`（56 通过 0 失败）全部通过。
- Mock server 烟雾测试：`tests/mock_server.py --port 8099` 启动后，全部 `/api/xxx` 端点响应符合契约，OTA/WiFi 扫描/WiFi 连接异步状态机正确推进，Captive Portal `/generate_204` 返回 302 Location: `/`。
- 编译：`idf.py build` 通过，app 大小 `0x131de0`，OTA 分区剩余 32%。

### 已知限制
- 配网页面 HTML/CSS/JS 仍以字符串内联到固件，未启用 gzip 压缩（待后续优化）。
- 前端无构建工具链（无 webpack/vite），`app.js` 手写 ES5 兼容 iOS Captive Portal WebView，不使用 ES6 模块。

### 项目结构与文档梳理（2026-06-29）
- 评估 `test/` 与 `tests/` 目录合并方案，结论**不合并**：
  - `test/` 按 ESP-IDF 生态约定保留给领域层 host 单测（纯 C，`test_domain.c` + `Makefile`，产物已 gitignore）。
  - `tests/` 放所有需要外部依赖（Python/bleak/pyserial/Node）的设备测试、静态门禁、mock、串口工具。
  - 合并需修改 15+ 文件路径引用（规则、文档、skill、quality_gate.py、test_runner.sh、.gitignore），风险高于收益；Makefile 路径反而要从 `../components/` 变 `../../components/`。
- 重写 `tests/README.md`：按 7 大分类组织（A 静态门禁 / B 动态自测 / C OTA 全流程 / D 本地 Mock / E 串口日志 / F 压力测试 / G 运行器），新增典型场景与 Q5（本机不能切换 WiFi 时用 mock_server）、Q4（bleak 超时重置蓝牙），附「test/ 与 tests/ 合并评估」专章。
- 重写 `docs/INDEX.md`：补全 workflows/ 全部 7 个流程规则索引、domain/ 2 个领域规则索引、test-coverage-analysis.md、tests/README.md、test/test_domain.c 入口；区分治理层（tier-1）/项目层（tier-2）规则。
- `.trae/rules/general-rules.md` 项目版本号 `v3.1.8 → v3.2.0`。

## [3.1.8] - 2026-06-28

### 测试（Portal 静态检查补强）
- `tests/check_portal_structure.py` 新增 `--file` 参数，便于用临时样本做回归验证。
- TC-3d 新增 Portal 回归检查：保存成功强反馈文案、`/ota/upgrade/status` 升级进度端点、OTA changelog JSON/HTML 转义链路、`/more` 个人信息前端范围校验。
- RED 验证：临时移除“已保存到设备”文案后，`python3 tests/check_portal_structure.py --file <tmp>` 正确失败。

### 测试报告（Portal 静态检查补强）
- `python3 tests/check_portal_structure.py`：46 通过、0 失败。
- `python3 tests/check_portal_js.py`：11 个 script 块语法全通过。
- `python3 tests/check_config_consistency.py`：87 通过、0 失败。
- `python3 scripts/tools/quality_gate.py`：通过；`idf.py build` 通过，host 单测 1154 断言通过，静态门禁聚合全绿；仅提示当前有本次测试/文档改动未提交。

### 改进（第八轮 docs 入口一致性）
- `README.md`、`docs/INDEX.md`、`PROGRESS.md`、`docs/user/04-ota-user-guide.md`、`docs/dev/01-technical-reference.md` 同步最近能力入口描述：OTA 升级进度反馈、OTA/WiFi JSON 与 XSS 加固、配置 JSON 序列化、静态门禁聚合、Release build_all 标准化、`/more` 校验、保存生效强反馈和 P0 真机验收清单。
- 本轮只改文档/入口说明，不新增文档，不改 C 代码。

### 测试报告（第八轮 docs 入口一致性）
- `python3 tests/check_config_consistency.py`：87 通过、0 失败。
- `python3 scripts/tools/quality_gate.py`：通过；`idf.py build` 通过，host 单测 1154 断言通过，静态门禁聚合全绿；仅提示当前有本次文档改动未提交。

### 改进（第七轮 P0 真机验收清单）
- `PROGRESS.md` 新增“第七轮 P0 真机验收清单（当前交付口径）”，集中区分已就绪交付物与待人工确认项，覆盖按钮/LED、配网、Captive Portal 多系统弹窗、OTA 真机验证、72h 老化和 Release 人工步骤。
- `PROGRESS.md` 修正阶段3二次确认说明，当前版本会进入阶段5配网链路，不再保留旧阶段“wifi_trigger 未注册”的过时描述。

### 测试报告（第七轮 P0 清单整理）
- `python3 tests/check_config_consistency.py`：87 通过、0 失败。
- `python3 scripts/tools/quality_gate.py`：通过；`idf.py build` 通过，host 单测 1154 断言通过，静态门禁聚合全绿；仅提示当前有本次文档改动未提交。

### 新增（部署脚本）
- 新增 `scripts/flash_latest.sh`：自动探测 ESP-IDF 路径和 Espressif USB JTAG/serial 串口，默认运行快速静态门禁、编译正式固件、烧录开发板并抓取启动日志。
- 脚本支持 `--erase`、`--monitor`、`--no-checks`、`--capture-seconds`、`--ota-test-auto-config` 等选项，避免手工反复输入 `idf.py build/flash/monitor`。
- `README.md` 烧录章节改为优先推荐一键脚本，同时保留手动 `idf.py` 流程。

### 修复（OTA 触发与射频独占保护）
- `main/wifi_portal.c` 修复 `/ota/upgrade` 创建 `ota_upgrade_task` 后未检查返回值的问题：内存不足或任务创建失败时不再向用户展示“正在升级”的假成功页，而是返回错误页并恢复 `s_closing=false`。
- `main/radio_ctrl.c/.h` 新增 `radio_ctrl_ble_suspended()`：OTA 独占 STA 或共存失败兜底主动停 BLE 后，`app_main` 不再误判为 BLE 故障并自动重启 NimBLE，避免 OTA 下载期间 BLE 与 STA 抢射频。
- 网络/OTA 辅助任务统一使用 `TASK_PRIO_NET`，TC-3g 对 `xTaskCreate` 栈大小、优先级和错误检查全绿。

### 改进（规则、文档与测试自动化）
- `AGENTS.md`、`.trae/rules/*`、`PROGRESS.md`、`docs/dev/01-technical-reference.md` 同步当前 v3.1.8 事实：普通配网走 coex 共存且 BLE 不停，OTA 独占 STA 才停 BLE；修正旧版“必须先停 BLE/配网后重启恢复”的过时描述。
- `tests/stage3_conn_test.py` 支持 `--port/-p` 并自动扫描 Espressif USB JTAG/serial 串口，修复 Linux 下默认写死 `/dev/cu.usbmodem101` 导致连接事件验证误失败。
- 新增 `tests/ota_local_mock.py`：从 `main/wifi_portal.c` 提取真实 `/ota` 页面 JS，在 localhost mock `/ota/check`、`/ota/status`、`/ota/upgrade`，无需连接 BikePower 热点即可验证 OTA 页面流程。
- 真机流程记录：测试固件 `OTA_TEST_AUTO_CONFIG=ON` 可自动进配网，串口确认 v3.1.8、BLE 广播、SoftAP/DNS/HTTP 启动，配网态堆约 116KB；本机 NetworkManager 连接 BikePower 后会因无互联网/路由切换不稳定，后续 OTA 页面流程改用本地模拟验证。

### 测试报告（本轮审计）
- Host 单测：`tests/test_runner.sh -d`，1088 通过、0 失败。
- 静态门禁：`check_portal_js.py`、`ota_flow_test.py --static`、`check_portal_structure.py`、`check_nvs_schema.py`、`check_board_desc.py`、`check_tasks_wdt.py`、`check_config_consistency.py` 全部通过。
- 编译：正式固件 `idf.py build` 通过，app 大小 `0x136a80`，OTA 分区剩余 31%；测试固件 `idf.py build -DOTA_TEST_AUTO_CONFIG=ON` 通过，app 大小 `0x136b90`。
- 真机：`idf.py -p /dev/ttyACM0 flash` 成功；`ble_selftest.py` 五服务/DIS/FTMS ERG 全通过；`stage3_conn_test.py -p /dev/ttyACM0` 连接事件通过。
- 部署脚本：`./scripts/flash_latest.sh --port /dev/ttyACM0` 成功完成快速静态门禁、正式固件 build、flash 和 15s 启动日志抓取；随后 `python3 tests/ble_selftest.py` 通过。
- 本地 OTA 模拟：`python3 tests/ota_local_mock.py --self-test` 通过，命中 `/ota`、`/ota/check`、`/ota/status` 轮询和 `/ota/upgrade`。

### 修复（OTA 版本检查失败：HTTP 302 重定向）
- `main/ota_downloader.c` 修复 Gitee `latest.json` 返回 302 时被当错误处理导致「无法获取版本信息」：ESP-IDF `esp_http_client` 的 `disable_auto_redirect=false` 不足，需显式设 `max_redirection_count`（默认 0 不重定向）。
- `check_version` 和 `download_and_upgrade` 两处 HTTP 客户端配置均添加 `.max_redirection_count = 10`，跟随 Gitee raw URL 的 302 跳转到实际文件位置。
- 真机日志确认：修复前 `latest.json HTTP 302 content_length=444`（读到重定向页 HTML）→ 修复后跟随重定向拿到真实 JSON。

### 修复（配网页面 JS 语法错误：检查更新按钮无反应）
- `main/wifi_portal.c` `/ota` 页 `pollStatus` 函数结尾多余的 `}` 导致 `setInterval(function(){...},2000)` 的 `)` 失配，整个 `<script>` 块报 `missing ) after argument list`，「检查更新」按钮点击完全无反应。
- 历史教训已记入 TC-3a 规则：任何 `main/wifi_portal.c` 改动必须先跑 `tests/check_portal_js.py` 静态检查。

### 新增（测试体系：覆盖率目标 90% + Loop 模式流程）
- 抽取 OTA 纯逻辑到领域层（host 可测）：
  - `components/bikepower_domain/ota_version.c/.h`：语义版本比较（从 `main/ota_update.c` 抽取），覆盖防降级核心逻辑。
  - `components/bikepower_domain/json_miniparse.c/.h`：latest.json 解析（从 `main/ota_downloader.c` 抽取 static 函数），覆盖版本检查字段提取。
- `test/test_domain.c` 新增 `test_ota_version_compare`（13 断言）+ `test_json_miniparse`（13 断言），host 单测断言数 581 → 946。
- 新增 `tests/check_config_consistency.py`（TC-3c 常量一致性静态检查）：56 项校验 `bikepower_config.h` / `domain_config.h` / `README.md` / `docs/` 之间常量值一致，防止文档与代码漂移。
- 新增 `docs/dev/03-test-coverage-analysis.md` 测试覆盖率分析文档：模块清单、覆盖率矩阵、当前 28% → 目标 90% 路径、Loop 模式测试建立流程。
- 更新 `.trae/rules/workflows/test-coverage.md`：TC-1 覆盖对象新增 ota_version/json_miniparse，新增 TC-3c 常量一致性规则，新增「覆盖率目标与现状」+「Loop 模式测试建立流程」章节。
- 抽取按钮按压时长分类到领域层（host 可测）：
  - `components/bikepower_domain/button_logic.c/.h`：短按/中区间/长按分类、REPEAT 调功判定、确认窗口超时（从 `main/app_state.c` 抽取）。
  - `main/app_state.c` `BTN_UP`/`BTN_REPEAT`/`app_state_tick` 改为调用领域层函数，消除硬编码阈值比较。
  - `test/test_domain.c` 新增 `test_button_logic`（18 断言），覆盖 500/2000ms 边界与自定义阈值。
- 抽取 LED 模式映射到领域层（host 可测）：
  - `components/bikepower_domain/led_pattern.c/.h`：pattern→GRB 颜色映射、闪烁节拍判定、初始 on 状态（从 `main/led.c` 抽取）。
  - `main/led.c` `led_output`/`led_task` 改为调用领域层函数，删除 `s_color_slow`/`s_color_solid`/`s_color_fast` 静态数组（颜色映射统一由领域层提供）。
  - `test/test_domain.c` 新增 `test_led_pattern`（15 断言），覆盖 4 种灯效颜色 + 闪烁周期边界 + 初始状态。
- `docs/dev/03-test-coverage-analysis.md` 更新覆盖率矩阵：领域层 864→944 行（+button_logic 38 + led_pattern 42），自动化覆盖率 27.7%→30.0%，host 单测断言 946→979。

### 改进（OTA 升级方式精简：仅保留在线下载）
- 移除 `/ota/upload` 路由及上传固件 bin 文件方式，统一为 HTTPS 在线下载升级，消除「上传 vs 在线」两种入口的认知冲突。
- `main/ota_update.c` 大幅精简：删除 multipart 流式上传、`ota_update_handle_upload`、`OTA_RECV_CHUNK`/`OTA_HEADER_BUF_LEN`/`OTA_FIRMWARE_MAX` 等上传相关实现与常量；仅保留自检确认/回滚逻辑，版本比较已抽到领域层。
- `main/wifi_portal.c` 修复防降级漏洞：`ota_check_task` 由 `strcmp != 0`（仅判断不同，含降级）改为 `ota_version_compare > 0`（仅远端更新才提示），避免误判降级为「有更新」。
- `main/wifi_portal.c` `h_ota_check`/`h_ota_upgrade` 不再接收表单 `ssid`/`psk` 参数，统一从 NVS 读取已配置的家庭 WiFi 凭证；未配置时返回错误引导去 `/more` 配置。
- `main/wifi_portal.c` `/ota` 页面重构：移除上传卡片与上传 JS，凭证改为只读展示已配置 SSID；未配置时显示「去配置家庭网络」入口；前端 `checkUpdate`/`startUpgrade` 不再发送凭证。
- `main/wifi_portal.c` `/more` 菜单「固件升级」描述由「上传新固件 bin 文件」改为「在线检查并下载最新固件」。
- `docs/user/04-ota-user-guide.md` 重写为在线下载流程，移除上传步骤与同版本重刷说明。
- `.trae/rules/general-rules.md` 模块树 `ota_update.c/.h` 描述更新为「自检确认/回滚 + 语义版本比较工具」。

### 修复（安全）
- `wifi_portal.c` h_ota 路由不再明文回显家庭 WiFi 密码到 HTML input value，避免 SoftAP 开放网络下密码泄露；密码框留空时从 NVS 加载已保存密码。
- `nvs_store.c` `nvs_store_load_wifi_cred` 支持 psk=NULL 可选参数，用于只加载 SSID 不暴露密码的场景。

### 新增（家庭网络配置 + WiFi 扫描优化）
- `/ui/module?name=wifi` 家庭网络面板：自动扫描周边 WiFi（信号最强前 10），用户选择后输入密码测试连接，凭证保存到独立 namespace `wifica`。
- `radio_ctrl.c` 新增 `radio_ctrl_wifi_scan`：APSTA 模式扫描，首次扫描时创建 STA netif，完成后切回 AP-only。
- `radio_ctrl.c` 新增 `radio_ctrl_sta_connect`：STA 连接家庭路由器获取 IP，用于 OTA 在线检查。
- WiFi 扫描/连接改为异步任务，避免阻塞 HTTP 线程导致 fd 耗尽。

### 改进（fd 耗尽修复）
- `wifi_portal.c` 所有 HTTP 路由（20 个）统一添加 `Connection: close` 头，强制关闭连接释放文件描述符，解决 `httpd_accept_conn: error in accept (23)`。

### 新增（Captive Portal 自动弹配网页）
- 自研轻量 DNS 服务器（`main/captive_portal.c`）：UDP 53 监听，所有 A 记录查询应答 192.168.4.1，手机连上 SoftAP 后任意域名都指向配网入口。零第三方依赖，DNS 任务栈 3072 字节，固件增量 ~2KB。
- OS 探测端点覆盖：注册 10 个主流系统连通性探测端点（`/generate_204` Android、`/hotspot-detect.html` iOS、`/ncsi.txt` Windows、`/canonical.html` Firefox 等），统一 302 重定向到配网首页，触发系统自动弹出 Captive Portal 窗口。
- DNS 生命周期集成到 `radio_ctrl`：AP 启动后启动 DNS 劫持，退出配网时停止，与配网会话同生命周期。DNS 启动失败降级为手动输入 IP 配网。

### 改进（4MB Flash OTA 分区表）
- `partitions.csv` 从 2MB factory 布局改为 4MB OTA 布局：`ota_0`/`ota_1` 互切（各 1.75MB）+ `otadata`，无 factory 分区。固件 1017KB，每分区 45% 空闲。首次烧录需 `esptool.py erase_flash`。
- `sdkconfig.defaults` 启用 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`：新固件首次启动处于 `ESP_OTA_IMG_PENDING_VERIFY`，应用自检通过调 `esp_ota_mark_app_valid_cancel_rollback` 确认；未确认即重启则 bootloader 自动回滚。
- `sdkconfig.defaults` 启用 `CONFIG_ESPTOOLPY_FLASHSIZE_4MB`。

### 改进（ESP-IDF 升级）
- ESP-IDF v5.5.1 → v5.5.4：phy_version 1201→1232，BT controller 2edb0b0→b7de11e，工具链 esp-14.2.0_20241119→esp-14.2.0_20260121。固件体积 +2.8KB（0xf7350→0xf7e60），编译/真机验证通过。
- 配网启动/停止路径加堆内存诊断日志（`wifi_portal.c`/`radio_ctrl.c`），便于内存监控。

### 新增（OTA 升级实现）
- `main/ota_update.c`：multipart/form-data 固件上传，流式写入 ota_1（不整包驻留内存，峰值 ~4KB）；解析 `esp_app_desc_t.version` 与 `FIRMWARE_VERSION` 比较，拒绝降级（<当前版本），同版本允许重刷。
- 自检回滚：新固件首次启动处于 `ESP_OTA_IMG_PENDING_VERIFY`，BLE 启动失败立即 `esp_ota_mark_app_invalid_rollback`；启动 10s 自检窗口到期未 abort 则 `esp_ota_mark_app_valid_cancel_rollback` 确认。
- 配网门户集成 `/ota` 路由：Web 上传固件页面，保存即升级，设备重启进新固件。WiFi 退出路径不变（done 回调 → ST_BLE_ACTIVE）。
- 真机验证：v3.1.4→v3.1.5 升级成功，v3.1.5→v3.1.4 降级被拒，自检确认有效。

### 修复（OTA 版本同步）
- `CMakeLists.txt` 强制 `PROJECT_VER` 从 `bikepower_config.h` 提取 `FIRMWARE_VERSION` 宏：原 `git describe` 含后缀导致 `esp_app_desc.version` 与宏不一致，防降级比较基准错乱。修复后 `app_desc.version=3.1.5` 与宏严格一致。
- 移除 `OTA_DEBUG` 调试 message，出正式固件 v3.1.5（无自动配网，靠按钮触发）。
- `OTA_TEST_AUTO_CONFIG` 改为 CMake option（默认 OFF）：正式发版编译不带，测试编译传 `-DOTA_TEST_AUTO_CONFIG=ON` 启用启动自动配网，支持 OTA 全流程自动化测试（升级+降级保护+版本确认），正式固件行为不受影响。

### 改进（文档）
- 新增 `docs/09-memory-and-feature-assessment.md`：各场景内存实测、满负载分析、OTA/Captive Portal/FTMS 扩展可行性评估与规划建议。
- 新增 `docs/10-ota-implementation-plan.md`：OTA 升级与 Captive Portal 落地方案，含分区表设计、multipart 上传协议、状态机扩展、回滚自检、防降级、实施步骤清单。

## [3.1.3] - 2026-06-27

### 改进（WiFi/BLE 共存配网，设备不重启）
- 配网期间 BLE 保持在线继续通知，骑行 App 全程不断连。原方案进入配网会停 BLE + 配网结束 `esp_restart()`，现改为共存模式（`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`），coex 模块按时间片仲裁 WiFi/BLE 射频。
- 配网结束（保存成功或 180s 超时）不再重启设备：`wifi_portal` 通过 done 回调投递 `APP_EVT_WIFI_DONE`，状态机收到后调用 `radio_ctrl_exit_wifi` 关闭 HTTP + SoftAP，回到 `ST_BLE_ACTIVE`。
- 配置变更实时生效：网页表单提交后通过 `power_engine_set_*` 系列 setter 同步内存与 NVS，主循环 50ms 周期通过 BLE 通知新值，无需重启。
- 失败兜底：仅当共存模式下 `radio_ctrl_start_wifi_ap()` 返回错误（资源耗尽/初始化失败）时，降级为旧独占方案（停 BLE + 重试 + 必要时 `esp_restart`）。

### 改进（配网页面交互）
- 首页底部按钮拆分为"保存设置"（仅保存+实时生效，不关 WiFi，可继续修改）和"保存并关闭 WiFi"（保存+实时生效+5s 后关 WiFi 回到 BLE）两个按钮，通过 `formaction` 区分目标路由。
- 去掉"恢复默认"按钮及对应 `resetForm` 函数，简化界面操作。
- 保存成功后首页 URL 带 `saved=1` 参数触发 toast 提示"已保存并实时生效"，2s 自动消失。

### 修复（代码逻辑漏洞）
- `wifi_portal.c` `h_home` 缺 `s_closing` 守卫：关 WiFi 收尾阶段浏览器刷新首页会续期 deadline 破坏收尾流程。修复：开头加守卫，收尾期间刷新跳转成功页。
- `wifi_portal.c` `h_draft` 草稿污染引擎：`apply_form_fields` 置 dirty 后主循环 10s 心跳会自动落盘 NVS，违反草稿语义。修复：草稿后立即 `power_engine_clear_dirty`。
- `wifi_portal.c` NVS 失败回滚 BLE 跳变：`apply_form_fields` 已改 `s_engine` 并触发 BLE 通知，回滚后下次通知旧值造成跳变。修复：改写临时副本，NVS 成功后才 swap 回主引擎。
- `wifi_portal.c` `send_save_error` 返回 `ESP_FAIL` 导致 httpd 重复发错误响应。修复：改返回 `ESP_OK`（500 响应已通过 SEND 完整发送）。
- `ble_bikepower` 设备名网页修改后不实时生效：BLE 广播名只在启动时调一次。新增 `ble_bikepower_refresh_device_name()`，保存路径检测设备名变化时刷新 GAP 服务+重启广播。
- `nvs_store.c` NVS 并发竞争：app_main 主循环（按钮调功 dirty 落盘）与 httpd 任务（网页保存）并发 `nvs_store_save` 无锁。修复：`StaticSemaphore` 互斥保护，100ms 超时跳过保留 dirty。
- `radio_ctrl.c` `esp_netif_deinit` 无条件调用：可能释放别人 init 的 netif。修复：加 `s_netif_inited` 标志，仅自建时 deinit。

### 修复（超时卡死 bug）
- `app_main.c` `on_wifi_done` 的 `xQueueSend(..., 0)` 零等待投递，队列瞬满时事件丢失；`portal_done` 的 `s_closing` 守卫又阻止重试，导致永久卡在 `ST_WIFI_CONFIG`，定时器继续跑每秒打印噪音。修复：改为 5 次重试投递（每次 10ms）+ 诊断日志。
- `wifi_portal.c` `shutdown_check_cb` 日志在 `portal_done` 幂等检查之前打印，导致每秒重复打印"到期"噪音。修复：加 `s_closing` 守卫，收尾后不再重复打印。
- `wifi_portal.c` `portal_done` 加链路诊断日志，确认 `done_cb` 调用与 `s_done_cb` 有效性。

### 修复（收尾流程逻辑漏洞，真机验证发现）
- `wifi_portal.c` `s_closing` 语义冲突导致"保存并关闭 WiFi"后 WiFi 永不关闭：`h_close_wifi` 设 `s_closing=true` 表示"5s 收尾窗口"，但 `portal_done` 和 `shutdown_check_cb` 都用 `s_closing` 做幂等守卫，语义冲突导致 `portal_done` 永不执行。修复：引入独立 `s_done_called` 标志做 `portal_done` 幂等，`s_closing` 仅表示"收尾窗口已启动/已结束"，两者解耦。
- `wifi_portal.c` `shutdown_check_cb` 守卫误用 `s_closing`：上一轮止噪修复用 `s_closing` 守卫，但也误杀了收尾窗口到期触发 `portal_done` 的能力。修复：改用 `s_done_called` 守卫，`s_closing` 状态下仍检查 deadline。
- `wifi_portal.c` `wifi_portal_start` 失败路径卡死状态机：`start_httpd`/`esp_timer_create`/`esp_timer_start_periodic` 任一失败直接 return，`APP_EVT_WIFI_DONE` 永不投递，状态机卡 `ST_WIFI_CONFIG`。修复：三处失败路径都调 `portal_done()` 触发 done_cb，状态机走 `radio_ctrl_exit_wifi` 幂等清理。
- `radio_ctrl.c` `wifi_portal_start` 失败注释过时：原注释说"等 APP_EVT_WIFI_DONE"，但该事件原实现不会来。修复：注释更新为"portal_done 已投递事件"。

### 改进（按钮诊断）
- `button.c` `post` 函数加按钮事件诊断日志，确认 ISR→button_task→app_queue 链路通畅。

### 改进（规则同步）
- `wifi-ble-mutex.md` 改名为"WiFi/BLE 射频共用规则"，更新 WB-MUTEX-1~8 条款，新增失败兜底降级路径。
- `ai-challenge.md` 硬件约束冲突条款更新为支持共存；操作权限 Deny 项由"BLE+WiFi 共存"改为"绕过 radio_ctrl 直接调 esp_wifi_*"。
- `coding-style.md` 禁止事项更新为"禁止绕过 radio_ctrl 直接调用 esp_wifi_*"。
- `best-practices.md` WiFi 健壮性、架构一致性、配网链路图同步更新。
- `README.md` 配网说明、LED 灯效表更新为共存方案。

### 测试报告
- `idf.py build`：0 error，固件大小 0xf70a0 字节（约 1MB），分区剩余 45%。
- host 单测：918 通过 0 失败。
- 真机测试：BLE 连接稳定（堆 180KB），网页保存实时生效，device_name 刷新验证通过。

## [3.1.2] - 2026-06-26

### 修复
- 配网页面表单值丢失：用户从首页切换到"更多配置"再返回后，首页显示的功率/踏频/心率/模式恢复为旧值，最终保存到 NVS 的也是错误默认值。根因有二：
  1. 浏览器缓存了配网首页 HTML，返回时显示缓存版本而非服务端最新状态。修复：首页响应添加 `Cache-Control: no-store, no-cache, must-revalidate, max-age=0`。
  2. "更多配置"页隐藏表单只包含 `device_name`/`profile_*` 字段，缺少 `mode`/`power`/`cadence`/`heartrate`。用户点"应用并回首页"提交到 `/draft` 时这四项未回传，`apply_form_fields` 无法恢复之前选择的值。修复：补全隐藏字段并新增 `mode_to_id()` 将整数模式映射为表单字符串。

### 改进（规则/记忆）
- 新增 `WIFI-28`/`WIFI-29` 规则：动态 HTML 必须禁缓存；多页表单每页隐藏字段必须覆盖全部可变字段。
- 踩坑记忆补充两条 WARNING：浏览器缓存与多页表单隐藏字段完整性。

### 测试报告
- `idf.py build`：0 error。
- 真机烧录 + 串口确认：固件 `v3.1.2`，BLE 五服务启动正常。
- Host 单测：581 通过，0 失败。

### 测试工具（沿用上版）
- `scripts/tools/quality_gate.py` 过时项修正：
  - 项目路径硬编码 `/Users/bytedance/...` 改为按脚本位置自动推导，跨机器可用。
  - IDF 激活从硬编码 `~/esp/esp-idf-v5.5.4` 改为默认 `/home/mengen1014/work/esp32-power/esp-idf`，并增加 `idf.py --version` 探测，已激活则不重复 source。
  - 子进程由 `/bin/sh` 改为显式 `bash -c`，修复 IDF `export.sh` bash 语法在 dash 下静默失败。
  - 编译判定由文本匹配 `Project build complete` 改为 `rc==0 且输出含 build complete 或 write_flash`，避免 `tail` 截断误判。
  - Host 单测判定增加 `None` 保护，原 `assertions.group(0)` 在输出异常时崩溃。
  - 必备资产清单同步实际目录。

## [3.1.1] - 2026-06-16

### 修复
- 板型自动检测误判：原用 GPIO12 电平判断，合宙通用板 GPIO12 接 LED 电路被内部下拉淹没，导致误判为 M5Stamp C3 走 SK6812 驱动致 LED 不亮。改用 GPIO3 检测（M5Stamp 按钮外部上拉读高，通用板悬空内部下拉读低），逻辑更可靠。
- 配网提交表单报 "Header fields are too long"（HTTP 431）：手机浏览器请求头超过默认 1024 字节，`CONFIG_HTTPD_MAX_REQ_HDR_LEN` 由 1024 提升到 2048。
- 配网首页表单跳转"更多配置"再返回后新填数值丢失：首页"更多配置"链接改为先把当前表单经 `/draft` 暂存到内存引擎再跳转；`/draft` 草稿保存扩展为也接受 `mode`/`power`/`cadence`/`heartrate`，返回首页时正确回显。

### 改进
- SoftAP 并发连接数 `max_connection` 由 3 降为 2，降低配网期内存峰值。
- 板型差异收敛为描述表：`board_detect.c` 用 `s_board_desc[]` 集中管理各板型的按钮/LED 引脚、LED 驱动类型与固件包名；新增 `led_drv_t` 驱动类型枚举，`led.c` 改为按驱动类型分发；新增板型只需追加枚举与一行描述，无需改动 led/button 等调用方。
- 新增编译期板型选择与双板型构建脚本（`scripts/build_all.sh`）。

### 文档同步
- 修正源码头注释与日志中残留的 ESP-IDF 版本号 v5.5.1 → v5.5.4（`app_main.c`/`button.h`/`wifi_portal.h`/`radio_ctrl.h`/`app_state.h`/`app_events.h`/`led.h`/`sdkconfig.defaults`）。
- 修正 `wifi_portal.c` `recv_body` 注释请求体上限 2048 → 4096，与代码一致。
- 同步 `docs/dev/01-technical-reference.md` 中 `max_connection` 3 → 2。
- 新增 `.clangd` 配置适配 RISC-V 工具链，消除 IDE 伪报错。

### 测试报告
- host 单测：581 通过，0 失败。
- 双板编译（`scripts/build_all.sh`）：generic-c3 / m5stamp-c3 均成功，app 分区 44% free。
- 真机测试：板型检测正确（generic-c3 GPIO3=0），BLE 五服务齐全，FTMS ERG 收敛正常，堆内存稳定 ~171KB 无泄漏。

## [3.1.0] - 2026-06-14

### 新增
- M5Stamp C3 开发板适配：运行时自动检测板型（GPIO12 电平判断），无需手动配置。
- SK6812 RGB LED 驱动（RMT bytes encoder）：M5Stamp C3 使用 GPIO2 驱动板载 RGB LED，颜色映射为蓝色（广播）/ 绿色（连接）/ 黄色（确认）。
- 板型检测模块 `board_detect.c/h`：启动时自动识别通用板或 M5Stamp C3，动态切换 LED/按钮引脚。

### 修复
- 配网页面模式切换失效：`syncMode()` 函数多余闭合花括号导致 JS 语法错误，整个 script 块无法执行。

### 测试报告
- `idf.py build`：0 error。
- 真机串口确认：板型检测正确（M5Stamp C3），SK6812 LED 正常闪烁，BLE 广播/连接/FTMS ERG 正常。
- 空闲堆：173KB（> 80KB 阈值）。
- 配网页面模式切换：已修复，三段切换正常。

## [3.0.0] - 2026-06-13

### 新增（阶段6 稳定性）
- Task WDT 覆盖：`app_main` 主循环与 `led_task` 订阅看门狗并周期喂狗；启用 `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0` 监控 IDLE 任务。
- 低内存监控：心跳日志按 `HEAP_WARN_BYTES`(40KB) / `HEAP_CRIT_BYTES`(20KB) 分级告警。
- Coredump：新增 `coredump` 分区（64KB @ 0x1d0000），ELF 格式写 flash + CRC32 校验。

### 新增（阶段5 WiFi 配网）
- `radio_ctrl`：唯一 BLE/WiFi 射频开关，方案 A 软切换（停 BLE → 释放射频 → 启 WiFi AP → 超时/保存后 `esp_restart()` 恢复 BLE）。
- `wifi_portal`：SoftAP + esp_http_server 配网门户，移植旧版完整 UI（iOS 三卡 + 训练区间染色 + 动态碎片页）；11 路由含 `/` `/config` `/success` `/more` `/draft` `/ui/module` `/closing` `/time` `/renew` `/mem_stats` `/favicon.ico`。
- 配网表单可改：功率/踏频/心率/模式/体重/FTP/阈值心率/配速机器人/设备名。
- `/success` 直访守卫（未提交配置 302 回首页）、`/favicon.ico` 返回 204、`/mem_stats` 返回堆内存 JSON。
- 测试脚本 `tests/stage5_portal_test.py`：HTTP 路由只读自测。

### 改进（测试脚本）
- `tests/ble_selftest.py`：补充 DIS 设备信息读取校验（厂商 `BikePower` / 型号 `ESP32-C3` / 固件 `3.0.0`），纳入总判定，与"五服务齐全"声明一致。

### 新增（阶段4 配置持久化）
- 领域层 `config_store`：配置 schema（defaults/validate/migrate），`CONFIG_VERSION=4`。
- `nvs_store`：NVS namespace `bikepower`，10 短 key 含 `devname`，脏标记周期落盘。

### 新增（阶段0-3 基线）
- 工程骨架（CMake/分区/sdkconfig）。
- NimBLE 五服务：CPS/HRS/CSC/FTMS/DIS + FTMS 控制点 ERG。
- 纯 C 领域层：功率引擎/心率模型/用户配置（host 单测可跑）。
- GPIO9 中断按钮 + GPIO12 LED 任务 + 7 态状态机。

### 测试报告
- host 单测（`test/test_domain`）：573 断言全过。
- `idf.py build`：通过，`bikepower-c.bin` 约 0xfd9a0 bytes，应用分区 43% free。
- 分区表核验：`coredump` 64K @ 0x1d0000 已写入。
- 真机老化、配网全链路、物理按钮/LED：待人工确认（见 [PROGRESS.md](PROGRESS.md)）。

### 已知限制
- ESP-IDF v5.5.4（已从 v5.5.1 升级，修复 NimBLE 断连问题）。
- WiFi/BLE 射频互斥，配网前必停 BLE。
- 发版前需将日志级别改为 `CONFIG_LOG_DEFAULT_LEVEL_WARN`。
