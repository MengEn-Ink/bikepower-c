# BikePower C — 项目进度

> 基线方案：[docs/archive/01-cidf-nimble-rewrite.md](docs/archive/01-cidf-nimble-rewrite.md)
> 当前固件版本：v4.2.1（C + ESP-IDF v5.5.4 + NimBLE，ESP32-C3）
> 主仓库：[github.com/MengEn-Ink/bikepower-c](https://github.com/MengEn-Ink/bikepower-c)
> 发布与部署：GitHub Release + GitHub Actions + GitHub Pages（唯一链路）

---

## 2026-11-05 仓库进一步精简

- [x] 清理六个祖先关系明确的已合并本地与远端分支；移除旧工作树并保留历史报告。
- [x] 删除未参与生产功能的 C 模板库实现和未调用板型接口，保留扫描与 DIS 身份数据结构。
- [x] 精简后 Host 6115/0、桥接生产会话/扫描/主备 sanitizer 及板型门禁通过。
- 文档、配图和 Pages 本轮结果见 [仓库治理报告](docs/dev/31-repository-hygiene-report.md)。

## 2026-11-05 源码清理与文档治理

- [x] 清除单设备重试常量、无生产调用接口和只写字段，补主任务/邮箱/快照契约与测试产物忽略规则。
- [x] ASan 精确短 JSON 复现并修复旧配置空地址越界；八组 E2E 夹具补主设备并防身份重复，已有备选保留地址。
- [x] 用户手册按钮方向、500ms 阈值及 3s 确认窗口对齐源码；同步八组/三设备 Code Wiki、测试入口和规则，归档两份已执行计划。
- [x] full operation gate 36 项及 3 分钟 15 轮耐久通过；修复项独立复验，最终 host/ASan/UBSan 6178/0。
- [x] 总质量门禁 `--no-build`：111 PASS / 2 dirty WARN / 0 FAIL；双板独立编译通过，app 均为 `0x113ac0`，factory 剩余 72%。
- [x] 源码事实、56 文档链接、30 操作覆盖矩阵及 diff 格式通过。
- `NOT_RUN`：当前候选实物验收，未检测到 ESP32 USB 串口。编译产物仅用于本地验证，未发布或打 tag。
- 详见 [源码清理与验证报告](docs/dev/30-local-cleanup-verification-report.md)。

## 2026-11-05 桥接主备恢复与多设备备选

- [x] schema v14、最多三台设备、地址类型 + 地址身份、bridge wire v2 与 v13/wire v1 迁移完成。
- [x] 八组 BLE 配置事务完成；`0009` 保留主设备及共享校正，`000a/000b` 保存两个备选设备。
- [x] 最近成功优先、60 秒两轮快速恢复、失败后完整模拟输出、每 60 秒轮转定向探测和立即重试完成；自动路径不扫描。
- [x] 三设备共享校正，真实 0W 不做偏移或比例计算；两份合法 0W 样本可确认来源。
- [x] Portal 三设备列表、首选/删除/满容量替换、配置/实际来源、倒计时与模拟降级交互完成；320/1440 三设备及 320 降级截图无横向溢出。
- [x] 主备静态契约已接入 operation gate 与总质量门禁。
- [x] 初轮 host 6175/0、桥接 sanitizer/八组协议/schema 专项、full operation gate 与 3 分钟 15 轮耐久通过；清理后结果见上节。
- [x] 初轮 quality gate 111 PASS / 2 dirty WARN / 0 FAIL；WARN 仅为当时未提交工作树及跳过最近提交 whitespace 检查。
- [x] 初轮 generic-c3 与 m5stamp-c3 双板构建通过：app 均为 `0x113af0`，factory 分区剩余 72%；该轮本地 merged bin 与校验文件不覆盖清理后的源码。
- [x] 旧真机证据绑定 Git `bf32b11`，脚本对当前基线 `255f82a` 正确拒绝；随后以空证据目录完成仅本地构建，测试报告明确未收录真机证据。
- `NOT_RUN`：两台以上真实或可控 CPS 来源的主备接管。
- `NOT_RUN`：重启后最近成功设备自动连接。
- `NOT_RUN`：全部设备离线后完整模拟输出连续性。
- `NOT_RUN`：设备恢复后每 60 秒低频探测并自动切回。
- `NOT_RUN`：真实热点、下游 App、上游功率计三方共存且空闲堆保持大于 80 KiB。
- `NOT_RUN`：原值、固定偏移、比例三种校正下的真实 0W。
- 设计与步骤见 [桥接主备设计](docs/dev/28-bridge-failover-multi-device-design.md) 和 [实施计划归档](docs/archive/18-bridge-failover-multi-device-implementation-plan.md)；未经上述实物证据不得发布或打 tag。

## 2026-11-05 WiFi 配置端交互收敛

- [x] profile 草稿与已应用模式不一致时隐藏旧实时输出，保存后才恢复对应实时数据；保存失败保留草稿并可重试。
- [x] 跑步模式统一广播、速度/步频、服务列表和本机会话术语；配置段编号连续，固定配速/配速档摘要及运动按钮选中语义同步。
- [x] schema、当前和最低空闲堆读取现有真实接口，不再展示硬编码诊断值。
- [x] 320/390/1440 生产压缩 DOM、正反切换、异常恢复及视觉检查通过；quick 操作门禁通过，Host 6146/0。
- [x] 内联 74,932 bytes，较 72,386 bytes 基线增加 2,546 bytes，低于 3 KiB 增量目标及 110 KiB 门禁。
- [ ] 真实热点、手机 Safari 和配网态最低堆水位仍需实物验收；本轮未修改 API、NVS、BLE 协议或固件 C。
- 设计与步骤见 [交互设计](docs/dev/26-wifi-portal-sport-mode-ux-design.md) 和 [实施计划归档](docs/archive/17-wifi-portal-sport-mode-ux-plan.md)。

## 2026-11-05 当前稳定性与异常覆盖

- [x] 压缩生产门户三屏宽 HTTP 异常、草稿保留、revision、丢响应/重复保存/等待超时、扫描取消和关闭重试。
- [x] Pages 实际响应匹配与 13 类事务异常；监听器释放、零未处理异常与恢复重试。
- [x] 生产 HTTP handler/cJSON ASan/UBSan：输入边界、revision、拒收/串事务、保存失败/重试和关闭。
- [x] 修复响应等待器泄漏、小数 revision、空成功结果和丢响应提示；接入操作/质量门禁与覆盖矩阵。
- [x] 最终完整专项质量门禁 110 PASS / 2 dirty WARN / 0 FAIL；双板正式配置构建、既有 Pages 布局/刷写回归通过。
- [x] full 操作门禁通过，host 6146/0、协议与运行时回归、3 分钟 17 轮核心流程耐久；最终 30 项操作矩阵校验通过。
- [x] 最终镜像通用 C3 串口、cycling/running、六组配置/NVS、双次重连、308.2 秒/258 条 CPS 耐久通过；扫描/连接/空通知失败均 0，完整原配置恢复并回读一致，独立 dirty 证据绑定镜像 SHA256。
- [ ] 真实热点、真实功率计三方共存、M5 实物、手机 Safari、按钮/LED、标准 App、掉电和真实 flash 写失败。
- [x] 修复及回归 `d3c9b8d`、板型及门禁 `4c3e6ac` 已提交推送至 main；文档治理 78 PASS / 2 dirty WARN / 0 FAIL，两份已执行计划归档。
- 详见 [稳定性异常报告](docs/dev/25-stability-exception-test-report.md)。最终提交远端 CI `41944208570` 已成功；未发布。

## 2026-11-04 Pages 分区与移动手册

- [x] 工具导航三分区、配置任务分组与运动类型渐进展开，保存保留其他组值。
- [x] 手机详细手册、四种按钮教学场景、播放/暂停/逐步、减少动画、实物人工自查。
- [x] `site-info.json` 独立页面构建时间与源码提交；两端页脚与正式固件发布时间分别显示。
- [x] 页面功能及刷写生命周期 36 项、站点生成 12 项、两主题六宽度交互与桥接配置保存回归通过。
- [x] 页面代码 `5305a04` 已于 2026-11-05 提交并推送。
- [x] 线上部署：Pages `41866576241` 于北京时间 2026-11-05 00:26:09 返回成功；产物与公网元数据为 `5305a04`，页脚显示构建时间 00:22。公网站点双主题手机/桌面四组合交互及 0 JS error 通过。
- [x] 基线工程 CI：`41866668168` 的压缩门户按钮超时保留历史失败；后续 `5c97e96` 的 `41867471119` 全部成功，Pages `41869049536` 部署成功。本轮稳定性异常修复仍需新提交 CI 验证。
- [ ] 实物按钮/LED、Safari 和真实功率计共存验收；教学与自查勾选不替代设备验收证据。

## 2026-11-04/05 自动优化收口

- [x] 六组配置 GATT 独立模块，通知整体快照、生命周期原子及 mailbox/门户短锁契约。
- [x] HTTP 独占共享 JSON 区，命名容量与超限错误；300 秒续期、done 重试/代次、重启取清生产回归。
- [x] 生产 GATT/通知/门户 cJSON ASan/UBSan，host 6146/0；完整质量门禁 107 PASS / 2 dirty WARN / 0 FAIL。
- [x] full 操作门禁及 3 分钟 17 轮耐久；双板独立构建通过，最终专项门禁 106 PASS / 0 FAIL（构建另有证据）。
- [x] 分层门禁、源码事实摘要、Markdown 锚点；同步当前文档，归档已完成计划，清理无构建/调用方的旧模板存储文件。
- [x] generic-c3 独立镜像启动、cycling/running、六组配置/NVS、GATT 重建、双次重连与 307 秒耐久通过；258 条 CPS，扫描/连接/空通知失败均 0，原配置恢复。
- [x] 补齐 C11 harness 的 POSIX 声明宏，本地 GCC 严格告警及 macOS ASan/UBSan 通过；后续基线 `5c97e96` 的远端 CI `41867471119` 成功。
- [ ] 热点 HTTP、真实功率计三方共存、M5 实物、Safari/手机、实体按钮/LED、标准 App 与原生浏览器授权。
- 证据见 [自动优化报告](docs/dev/22-automatic-optimization-report.md)；代码已分批提交推送，实际验收镜像仍绑定构建时 dirty 身份，旧候选包与旧证据保留，未发布。

## 2026-11-04 核心门户与真实功率桥接实施

- 发布门禁：代码提交与合并已获授权，v4.2.1 候选固件须用户手动验证并明确批准后才能发布。
- [x] 桥接领域、配置 schema v13、BLE 第六组及 NVS 显式编码；保留历史配置迁移。
- [x] host `6146/0`、桥接解析/校正/失效/配置往返 suite、13 个协议固定向量交叉校验通过。
- [x] 上游静态连接/订阅/DIS/重连、主循环统一输出和 revision 配置事务；生产会话与扫描 ASan/UBSan 通过。
- [x] 亮色核心门户、5 分钟显式续期、固定/机器人合并、DIS 复制及默认关闭心率；压缩页面 320/390/1440 交互通过，内联约 70.4 KiB。
- [x] 修复后 quality gate `101 PASS / 2 WARN / 0 FAIL`，full operation gate 通过，3 分钟耐久 17 轮无失败；WARN 为未提交工作区及由此跳过的历史提交 whitespace 检查。
- [x] 精简固件名称及双板正式构建；修复真机配置事务栈溢出，启动复测通过。
- [x] 固件及 DIS 标识按 `<板型>-v4.2.1` 统一；Pages 源码补齐来源与校正表单，六组保存、绑定保留及旧固件五组降级浏览器回归通过。Pages 已随 `5305a04` 部署。
- [x] 正式镜像启动、骑行/跑步、六组配置/NVS、GATT 重建、双次重连与 5 分钟耐久；302.2 秒、259 条 CPS、0 扫描/连接/空通知失败。测试后恢复原配置，报告见 `docs/dev/17-core-portal-bridge-test-report.md`。
- [ ] 配网及上下游桥接共存：macOS 热点关联 `-3900`，附近无独立真实功率计；M5Stamp 真机、Safari、物理按钮/LED 与骑行 App 人工验收未覆盖。

## 2026-10-30 v4.2.1 发布验收

- [x] host `6144/0`、全量 operation gate、clean build、generic-c3 烧录与串口启动通过。
- [x] BLE 五服务、四路通知、FTMS ERG、配置五组读写/错误事务、NVS 重启持久化、GATT 重建、广播掩码与双次重连通过。
- [ ] WiFi Portal 真机验收：经用户明确豁免；连接设备热点会中断当前发布网络，本次未覆盖。
- [ ] 物理按钮与 LED 灯效：需要人工观察，本次未覆盖。

## 阶段进度总览

| 阶段 | 内容 | 状态 | 验收 |
|------|------|------|------|
| 0 | 工程骨架（CMake/分区/sdkconfig） | ✅ 完成 | 空工程启动日志 |
| 1 | NimBLE CPS 单服务最小闭环 | ✅ 完成 | 手机搜到 BikePower 看到功率 |
| 2 | 领域层移植 + 五服务 + FTMS ERG | ✅ 完成 | 五服务通知 + ERG 收敛 + host 单测持续扩展（本轮基线 6093 断言） |
| 3 | GPIO 中断按钮 + LED 任务 + 状态机 | ✅ 代码完成（物理按钮待人工确认） | 短/中/长按 + 二次确认 + LED 各态 |
| 4 | 配置持久化（NVS load/save/migrate） | ✅ 完成 | 调功后断电重启功率由260W恢复为200W，host 单测持续覆盖 |
| 5 | WiFi 配网（radio_ctrl 共存 + AP + HTTP，设备不重启） | ✅ 代码完成（配网真机流程待人工确认） | 长按确认→配网→保存→关 WiFi 回到 BLE，BLE 全程不断 |
| 6 | 稳定性与发版（WDT/coredump/老化/tag） | ✅ 完成 | v3.0.0 已发版 |
| 7 | M5Stamp C3 适配（板型检测/SK6812/配网页面修复） | ✅ 完成 | v3.1.0 已发版 |
| 8 | WiFi/BLE 共存配网（v3.1.3） | ✅ 代码完成（真机待确认） | BLE 全程在线 + 配置实时生效 + 不重启 |
| 9 | Captive Portal + OTA 实现（v3.1.5） | ✅ 完成 | DNS 劫持自动弹配网页 + 4MB OTA 分区表 + OTA 防降级/自检回滚，真机升级降级测试通过 |
| 10 | 家庭网络配置 + OTA 在线检查（v3.1.6-v3.1.8） | ✅ 代码完成（真机待确认） | WiFi 扫描选网 + STA 连接路由器 + SNTP + HTTPS 拉 latest.json + 在线下载升级；进度反馈 + JSON/XSS 加固 + fd 耗尽修复 + 密码不回显 |
| 11 | 配网门户前后端分离（v3.2.0） | ✅ 代码完成 + 真机 BLE 自测通过 | 21 HTML handler → 15 `/api/xxx` JSON + 前端三件套 `main/web/` + `inline_static.py` 编译时内联 + `mock_server.py` 本地调试 |
| 12 | 项目结构与文档梳理（v3.2.0） | ✅ 完成 | `test/` 与 `tests/` 合并评估（不合并）+ `tests/README.md` 7 大分类重写 + `docs/INDEX.md` 补全 workflows/domain 规则索引 |
| 13 | 移除 OTA + 家庭网络 + 网页端精简（v4.0.0） | ✅ 代码完成（真机待确认） | 删 ota_downloader/ota_update/ota_version/ota_selftest + `/api/ota/*`、`/api/wifi/*`、`/api/draft` 路由与前端视图；分区表双 OTA → 单 factory（仍 4MB）；关回滚配置；门户退化为纯 BLE 参数页（方案见 [docs/archive/07-remove-ota-plan.md](docs/archive/07-remove-ota-plan.md)） |
| 14 | Windows 开发环境适配 | ✅ 完成 | `idf_common.psm1` 共享模块 + `dev.ps1` 统一入口 + `build_all.ps1`/`flash_latest.ps1` 重构去重 + Python 脚本跨平台串口探测 |
| 15 | BLE Monitor Pages + 网页固件刷写 | ✅ 已上线（双板真机刷写待确认） | GitHub Release v4.1.1 双板 merged bin 经 Actions 校验并部署；ESP Web Tools 0x0 manifest；刷写引导/主题弹窗与骑行设备过滤已完成 |

---

## 2026-10-27 Pages 全量 NVS 配置读取与修改

- [x] 新增始终可发现的自定义 BLE 配置服务，五个配置组覆盖当前 `config_data_t` 的全部 NVS 持久字段，最长负载适配 MTU 64。
- [x] 写入采用 BEGIN → 分组写入 → COMMIT/CANCEL 事务，revision 防止页面以旧快照覆盖设备上的新配置。
- [x] NimBLE host 回调只暂存配置并投递主循环；主循环在引擎副本上校验、应用和写 NVS，成功后才替换正式运行态，失败保持旧配置。
- [x] Pages 页面支持全字段读取、修改、重新读取、恢复设备值、JSON 导出、行内校验、错误摘要、保存进度和 NVS 最终结果反馈；旧固件无配置服务时保持实时监控并显示降级提示。
- [x] Profile / 广播掩码改变时等待页面断开后重建 GATT；仅设备名变化时保持连接并刷新 GAP 名称。
- [x] host `6123/0`、ESP-IDF `build-codex` 完整构建、BLE 监控静态门禁、站点生成器 7 项回归、内联 JS、桌面/375px 表单布局与 `git diff --check` 均通过。
- [ ] 真实开发板 Web Bluetooth 全量读取、事务写入、NVS 断电恢复、Profile/广播切换后重广播仍待真机验证。

---

## 2026-10-27 Pages 实时曲线增强

- [x] 曲线历史由固定最近 30 秒扩展为整次浏览器连接会话，默认全程显示，并提供 30 秒、2 分钟、10 分钟、全程窗口切换。
- [x] X 轴按会话经过时间动态增长；超过通知停更阈值的空档拆成独立线段，设备断连后仍保留曲线、统计和导出数据。
- [x] 支持鼠标、触摸和键盘逐点查看功率、踏频、心率、速度与毫秒时间；三条曲线增加不同线型和独立显隐控制。
- [x] 增加可见区间最小/平均/最大值、时长和样本数统计，以及暂停跟随、CSV 导出和二次确认清空。
- [x] 浏览器内最多保留 12 小时或 50000 个样本，长曲线绘制前执行分段与保峰降采样；JSON 报告同步携带会话历史。
- [x] 静态门禁、站点生成器 7 项回归、180 样本浏览器交互与桌面/375px 双视口检查通过；未触发真实 Web Bluetooth 授权，真机通知显示与长时会话仍待用户手动连接确认。

---

## 2026-10-27 Pages 刷写引导与骑行设备过滤

- [x] 刷写页按插线、选串口、等完成重构为三步新手流程，并补充耗时、英文状态、Erase 风险和串口排错说明。
- [x] ESP Web Tools Installing / 未选择串口网页弹窗适配信号宣言深黑锈橙主题；浏览器原生串口选择窗口保留系统外观。
- [x] Web Bluetooth 首次授权仅显示广播 CPS、HRS、CSC、FTMS 的设备；RSC 仍可在已入选设备上读取，但仅跑步设备不会进入骑行列表。
- [x] 已授权设备在 GATT 连接后再次复核运动服务，不符合骑行筛选条件时主动断开并提示。
- [x] 静态门禁、站点生成器 7 项回归、双视口浏览器检查、真实 ESP Web Tools v10.4.0 弹窗注入和项目级质量门禁均通过；本轮未触发真实 Web Bluetooth / Web Serial 授权或写入硬件。

---

## 2026-10-27 真实骑行数据模型优化

- [x] 参考 90 分 23 秒 MyWhoosh FIT 的 1Hz 功率、踏频、心率时序特征，提取秒差、自相关、功率/踏频相关和功率→心率滞后基线。
- [x] Steady/Road/ERG 功率统一改为 6-18 秒连续航点趋势 + 秒级相关扰动的双时间尺度有界模型；Road 不再逐拍独立取样，ERG 保持目标 ±5W 的相关波动。
- [x] 按 FIT 1Hz 秒差二次拟合，把快层跟随系数从 0.65 调到 0.70；默认 ±50W 总边界不变，固定种子秒差 P95 从 11W 提高到 12W，AC1 从 0.960 调整到 0.955。
- [x] 踏频增加与功率偏移的弱耦合，仍严格遵守用户配置的浮动幅度。
- [x] 联动心率增加 8 秒上升、15 秒下降的负荷功率滤波，以 85% 阈值心率为锚点压缩映射幅度，并增加 ±7bpm、30-90 秒连续独立游走。
- [x] 新配置默认浮动调整为功率 ±50W、踏频 ±8rpm；schema v12 仅把仍等于历史默认 ±5W 的功率浮动迁移为 ±50W，其他用户自定义值不覆盖。
- [x] 真机启动以 `esp_random()` 播种领域 RNG；host 仿真显式固定种子以保持可复现。
- [x] 新增 FIT 固定基准提取/复验、16 种子统计回归、24 小时等效快照、极值/退化/模式切换和 1Hz 抖动/丢样/重复保持仿真，以及 v11 历史默认/用户自定义迁移回归，当前 6093 断言通过。
- [x] 调研 SmartSpin2k、qdomyos-zwift 等开源实现，只独立借鉴加速仿真、时序压力和边界场景方法，未复制 GPL 源码。
- [x] 完整验证通过：Host 6093/0、ASan/UBSan 6093/0、full operation gate、3 分钟核心流程 6 轮耐久、ESP-IDF build、quality gate 99 PASS / 0 FAIL；固件 `0x116410`，factory app 分区剩余 71.95%。
- [x] generic-c3 完成烧录、启动、cycling 五服务/DIS、四路 1Hz 通知、FTMS ERG 与双次重连；连接压力 A/B 将广播间隔从 500ms 缩短到 100ms 后连续 7 轮无失败。
- [x] generic-c3 保留旧 NVS 升级后确认 `sv=12/pfloat=50`；90 秒 steady CPS 为 `226-303W`、`258.7±22.4W`、秒差 P95=`12W`，修复前仅 `258-265W`、标准差 `2.2W`、秒差 P95=`1W`。
- [ ] m5stamp-c3、running profile、WiFi/BLE 共存、标准客户端、物理按钮/LED 和设备长时运行仍待真机验证。

---

## 2026-10-22 P0 协议与会话稳定性收口

当前工作区已完成源码修复与离线门禁，尚未形成新的双板真机验收结论：

- [x] CPS/CSC、FTMS Bike/Treadmill、RSC/HRS 统一经生产 `ble_payload_codec` 编码；独立 JSON golden vectors 同时验证 C encoder 与 Python parser。
- [x] RSC 距离单位修正为 0.1m，RSC Feature 修正为 2 字节 `0x0007`；FTMS Treadmill flags 修正为 `0x0504`。
- [x] 跑步 Start/Stop/Reset 接入非持久化 running 会话；暂停归零瞬时运动量并冻结距离/用时，Reset 清累计量且保持运行/暂停态。
- [x] ERG 改为非持久化会话；设定/清除目标不污染基础功率，持久化映射统一由 `config_store_from_engine()` 规范化，旧 NVS ERG 值加载时回退 steady。
- [x] WiFi 完成通知增加 pending latch 与可确认回调，主循环可靠消费，错误态退出仍统一幂等清理 HTTP/DNS/SoftAP。
- [x] 无设备门禁增加控制点接纳/标准 Status 契约、双次同设备重连、running 四阶段、profile-aware fake harness 与 WiFi done 静态契约；真机 profile harness 测试时强制 mask=7 并恢复原 profile/mask 与完整 running 持久配置，running 自测按四阶段逐 opcode indication + 多样本严格判定。
- [x] 整合后执行完整 `quality_gate.py`：81 PASS、2 个预期 dirty-worktree WARN、0 FAIL；包含 `idf.py build`、5892/0 host 断言与全部离线协议/harness 门禁。
- [ ] generic-c3 与 m5stamp-c3 分别执行 cycling、running `--control`、双次独立重连、广播开关、配网退出和标准客户端验证；结果写入当前 Git ID 对应证据。

历史 release/tag 的 PASS 只说明当时固件，不覆盖上述当前工作区。离线 vectors 与 fake harness 也不能替代 NimBLE GATT 和物理链路验收。

---

## 第七轮 P0 真机验收清单（当前交付口径）

本清单用于交付前集中跟踪“代码/自动化已就绪”和“仍需人工真机确认”的边界。未勾选项不得在发版说明中标为已完成；物理按钮、LED、系统级 Captive Portal 弹窗和浏览器真机刷写保留人工确认（P1，不阻塞发版）；72h 老化降级为可选的长期观察项，发版阻塞项为 5 分钟设备耐久 `soak_5min`。

### 就绪交付物

- [x] 固件版本统一为 `v4.1.1`，DIS 固件字段与门户展示包含板型、版本号、稳定版、修复版和最后 Git 短提交 ID，`tests/ble_selftest.py` 会校验厂商/型号/固件三字段。
- [x] 普通配网链路走 `radio_ctrl` + coex 共存：长按二次确认进入配网，BLE 默认不停，保存或超时后关闭 WiFi 原地回 `ST_BLE_ACTIVE`。
- [x] 配网门户路由与前端校验已具备自动化覆盖：`tests/stage5_portal_test.py`、`tests/check_portal_structure.py`、`tests/check_portal_js.py`。
- [x] Captive Portal DNS 与 10 个系统探测端点已实现：Android/Chrome、iOS/macOS、Windows、Firefox、Kindle 探测路由统一 303 到首页，并注册 404 兜底覆盖未知探测路径。
- [x] 核心数值可配置已就绪：固定/配速机器人模式分段切换、功率/踏频双时间尺度动态、心率锁定/二阶联动，均经 `/api/config` 持久化到 NVS。
- [x] 配网入口交互已补强：设备名铅笔编辑、保存成功强反馈、不关闭 WiFi 时可继续修改。
- [x] 配置导入/导出基础能力已落到领域层：`config_store_serialize` / `config_store_deserialize` 支持 JSON 往返、缺字段默认值和范围夹紧。
- [x] Release 产物标准化脚本已就绪：`scripts/build_all.sh` 可生成 `<板型>-v<版本>.bin` merged bin、SHA256、manifest；Git ID 留在 manifest。脚本不自动打 tag、不创建 GitHub Release。
- [x] 自动化门禁入口已就绪：`tests/check_config_consistency.py` 与 `scripts/tools/quality_gate.py` 覆盖配置一致性、host 单测、构建和静态检查聚合；`tests/operation_gate.py --level full` 已接入 release 产物门禁和核心流程耐久。
- [x] 稳定交付证据链已接入：`scripts/tools/device_acceptance.py` 统一记录真机自动项与人工项，`operation_gate.py --level device` 可写入自动结果，正式 GitHub 发布要求当前版本与 Git ID 的 P0 证据全部通过。
- [x] Config Service 真机 P0 门禁已接入：五组全量读取、事务拒绝路径、五组修改、NVS 重启持久化、广播掩码触发 GATT 重建和 `finally` 原配置恢复由 `tests/ble_config_e2e.py` 自动验证。
- [x] 历史 generic-c3 真机自动化记录：v4.1.1 release 固件曾通过启动、骑行 BLE 与一次连接/断开，详见 `CHANGELOG.md` 的“v4.1.1 通用版真机验收”。该记录不覆盖 2026-10-22 当前工作区；新协议、跑步会话、双次重连、配网门户、广播开关、物理按钮/LED 和 m5stamp-c3 均须按当前 Git ID 重新验收。

### 待人工确认（P0）

- [ ] 按钮短按：短按一次后串口出现 `短按 功率-20W`，BLE 通知功率同步下降 20W。
- [ ] 按钮长按调功：steady 模式下按住约 1s，串口出现多次 `adjust`，功率按 20W 步进上升。
- [ ] 按钮长按确认：按住 ≥2s 松开后出现 `长按2秒，LED快闪等待二次确认`，LED 进入确认快闪。
- [ ] 二次确认进入配网：确认窗口内再按一次，串口出现 `二次确认，进入WiFi配网模式`，设备开放 `BikePower-XXXXXX` 热点。
- [ ] 确认窗口超时：不二次按键等待 3s，串口出现 `确认窗口超时` 并回到 BLE 活跃态。
- [ ] LED 灯效人工观察：通用板广播慢闪/连接常亮/确认快闪；M5Stamp C3 蓝色慢闪/绿色常亮/黄色快闪。
- [ ] 配网真机流程：手机连接 `BikePower-XXXXXX` 热点，访问 `192.168.4.1`，切换固定/配速机器人模式、修改功率/踏频/心率/浮动幅度/设备名并保存，确认 BLE 保持在线且参数实时生效。
- [ ] 配网关闭流程：点击“保存并关闭 WiFi”后 5s 收尾退出，HTTP/SoftAP/DNS 停止，设备不重启并回到 BLE。
- [ ] 配网超时流程：当前 300s 无续期自动关闭 WiFi，回到 BLE，串口无异常重启。
- [ ] Captive Portal 多系统弹窗：iOS、Android、Windows 分别连接 `BikePower-XXXXXX` 热点后能自动弹出配网页；不弹窗时手动访问 `192.168.4.1` 仍可配网。
- [ ] Captive Portal 生命周期：退出配网后串口确认 DNS 任务停止，不再响应劫持。
- [ ] 72h 老化：长时间运行无 coredump、无 WDT、无堆持续下降；建议周期性跑 `tests/ble_selftest.py` 和 `tests/stage5_portal_test.py` 采样。

上述未完成项继续保持待验收状态。执行时按板型写入 `tests/reports/device-acceptance-<板型>.json`；`NOT_RUN` 不视为通过，正式发布前必须对**参与发布板型**运行 `python3 scripts/tools/device_acceptance.py validate --strict --file <证据文件>`（P0 自动项为串口启动、BLE 全量配置读写与 NVS 持久化、骑行 BLE、重连、5 分钟耐久；跑步/门户/广播与人工项为 P1 不阻塞）。
- [x] Release tag：`v4.1.1` 已创建并推送，指向提交 `a9f127c`。
- [x] GitHub Release `v4.1.1` 已完成原样迁移：10 个顶层附件、双板固件及总包 SHA256、ZIP 结构和历史 manifest 记录均已复核；历史总包只有 generic-c3 证据，不冒充 m5stamp-c3 真机验收。

---

## 阶段3 完成情况（当前）

### 已实现

| 模块 | 文件 | 说明 |
|------|------|------|
| 按钮 | [main/button.c](main/button.c) | GPIO9 ANYEDGE 中断 → 信号量唤醒 button_task → 去抖/计时/自动重复 → app_queue |
| LED | [main/led.c](main/led.c) | GPIO12 独立任务，OFF/SLOW(1s)/SOLID/FAST(200ms) 四态 |
| 状态机 | [main/app_state.c](main/app_state.c) | 7 态枚举，消费按钮/连接事件，调功 + 二次确认窗口 + LED 驱动 |
| 事件 | [main/app_events.h](main/app_events.h) | BTN_DOWN/REPEAT/UP + CONN_CHANGED |
| 主循环 | [main/app_main.c](main/app_main.c) | 事件队列 drain + 50ms 快照 + 连接轮询 + TWDT 喂狗 |

### 按钮语义（与 MicroPython 行为一致）

| 操作 | 时长 | 动作 |
|------|------|------|
| 短按 | <500ms | 功率 -20W |
| 长按调功 | ≥500ms | 每 200ms 功率 +20W（仅 steady 模式） |
| 长按确认 | ≥2000ms 释放 | 回滚本次自动调功，进入确认窗口（LED 快闪） |
| 二次确认 | 窗口内再按 | 通过 `wifi_trigger` 回调触发 WiFi 配网 |
| 窗口超时 | 3000ms | 取消，恢复 BLE_ACTIVE |

### 真机验证（已通过）

- 三任务启动：LED/STATE/BTN 日志正常，state=ST_BLE_ACTIVE
- BLE 连接事件驱动状态机：连接 `connected=1`，断开 `connected=0`
- 空闲堆 220KB

### 待人工确认（需物理操作）

- [ ] 短按一次 → 串口出现 `短按 功率-20W`
- [ ] 按住 1s 松开 → 串口出现多次 `adjust` 且功率上升（steady 模式下）
- [ ] 按住 ≥2s 松开 → `长按2秒，LED快闪等待二次确认`，LED 快闪
- [ ] 快闪窗口内再按 → `二次确认，进入WiFi配网模式`，进入阶段5配网链路
- [ ] 不按等待 3s → `确认窗口超时`
- [ ] LED 灯效：广播慢闪 / 连接常亮 / 确认快闪

---

## 阶段4 完成情况

### 已实现

| 模块 | 文件 | 说明 |
|------|------|------|
| 配置 schema | [components/bikepower_domain/config_store.c](components/bikepower_domain/config_store.c) | 纯 C，defaults/validate（逐字段越界夹紧）/migrate（版本不匹配重置默认），可 host 单测 |
| NVS 桥接 | [main/nvs_store.c](main/nvs_store.c) | namespace=bikepower 短key，load（缺失/版本不匹配用默认并回写）+ save（脏标记落盘） |
| 主循环接入 | [main/app_main.c](main/app_main.c) | 启动 load + clear_dirty，10s 心跳脏标记落盘 |

- NVS namespace `bikepower`，schema v12；短 key 包含 `sv`/`power`/`cadence`/`mode`/`hr`/`hashr`/`pfloat`/`cfloat`/`hrfloat`/`pstep`/`weight`/`ftp`/`thr_hr`/`ride`/`cwkglo`/`cwkghi`/`cname`/`sport`/`bmask`/`rspeed`/`rpacef`/`rcad`/`rcadf`/`rmode`/`rpacer`/`rplo`/`rphi`/`rcname`/`devname` 以及 `led*` 场景配置。v11 中仍为历史默认 `pfloat=5` 的设备会迁移到 `pfloat=50`，其他自定义值保持不变。
- 旧 MicroPython JSON 配置不迁移，首刷重置为默认

### 真机验证（已通过）

- 首刷启动用默认值并回写：`p=260W c=90RPM mode=0 hr=150 w=70 ftp=300 thr=190 ride=0`
- 短按 3 次（每次 -20W）→ 等待落盘 → 断电重启 → 功率恢复为 `p=200W`，证明持久化生效
- 当时 host 单测 5841 断言全过（阶段 4 历史记录）；2026-10-22 本轮基线为 5892 断言，最终提交以最新完整执行结果为准。

---

## 阶段5 完成情况

### 已实现

| 模块 | 文件 | 说明 |
|------|------|------|
| 射频控制 | [main/radio_ctrl.c](main/radio_ctrl.c) | 唯一 WiFi/BLE 射频入口；配网走 coex 共存（BLE 不停），共存失败才降级为停 BLE 兜底 |
| 配网门户 | [main/wifi_portal.c](main/wifi_portal.c) / [main/web/](main/web) | esp_http_server（SoftAP 由 radio_ctrl 启动）；前端三件套 `main/web/` 由 `scripts/inline_static.py` 编译时内联到固件；C 侧仅保留静态首页、`/api/xxx` JSON、Captive Portal 探测路由和 404 兜底 |
| 状态机接入 | [main/app_state.c](main/app_state.c) | ST_WIFI_CONFIRM → ST_WIFI_CONFIG，经 `wifi_trigger` 回调触发 radio_ctrl |

- 超时方案：当前 300s（`WIFI_SHUTDOWN_MS`）无续期自动关闭 WiFi 回到 BLE，保存并关闭后 5s 收尾再退出配网
- 前端每秒轮询 `/api/time` 显示剩余时间，`/api/renew` 续期；`/api/mem_stats` 返回堆内存 JSON，`/favicon.ico` 返回 204
- 配网表单可改：功率/踏频/心率/模式/体重/FTP/阈值心率/配速机器人/设备名，保存后写入 power_engine + NVS

### 待人工确认（需配网真机流程）

- [ ] 长按 + 二次确认 → 进入配网，手机连 `BikePower-XXXXXX` 热点
- [ ] 浏览器访问 `192.168.4.1` → 修改参数 → 保存并关闭 WiFi → BLE 保持/恢复在线且参数生效
- [ ] 300s 无续期 → 自动超时关闭 WiFi 回到 BLE
- [ ] 连上热点后跑 `python3 tests/stage5_portal_test.py` 校验 HTTP 路由（首页、`/api/state`、`/api/time`、`/api/config`、`/api/renew`、`/api/close-wifi`、Captive Portal 探测与 404 兜底）

---

## 阶段6 完成情况

### 已实现

| 项 | 文件 | 说明 |
|------|------|------|
| Task WDT 覆盖 | [main/app_main.c](main/app_main.c) / [main/led.c](main/led.c) | app_main 主循环与 led_task 均 `esp_task_wdt_add(NULL)` + 周期 `esp_task_wdt_reset()`；IDLE 任务由 `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0` 监控 |
| 低内存监控 | [main/app_main.c](main/app_main.c) | 心跳检查空闲堆：低于 `HEAP_WARN_BYTES`(80KB) 告警，低于 `HEAP_CRIT_BYTES`(20KB) 严重告警 |
| Coredump | [partitions.csv](partitions.csv) / [sdkconfig.defaults](sdkconfig.defaults) | 新增 `coredump` 分区（64KB @ 0x1d0000），ELF 格式写 flash，CRC32 校验 |

- button_task 为中断驱动、空闲时无限阻塞等唤醒，**刻意不订阅 WDT**（订阅会在无按键 5s 时误复位）
- 阈值常量集中在 [main/bikepower_config.h](main/bikepower_config.h)：`HEAP_WARN_BYTES` / `HEAP_CRIT_BYTES`

### 待人工确认（需真机 + 不可逆操作）

- [ ] 72h 老化测试：长时间运行无 coredump、无堆持续下降（跑 `tests/stage5_portal_test.py` / `ble_selftest.py` 辅助）
- [x] 发版前 `sdkconfig.defaults` 日志级别改 `CONFIG_LOG_DEFAULT_LEVEL_WARN`（已设为 WARN）
- [x] DIS 固件版本号与 `FIRMWARE_VERSION` 同步确认（generic-c3 真机 `ble_selftest.py 8` 已校验厂商/型号/固件三字段）
- [x] `v4.1.1` tag 已创建并推送
- [x] GitHub Release 附件完整性复核（10 个顶层附件、三组 SHA256、ZIP 与 manifest）

---

## 阶段7 完成情况（v3.1.0）

### 已实现

| 模块 | 文件 | 说明 |
|------|------|------|
| 板型检测 | [main/board_detect.c](main/board_detect.c) | 运行时自动检测通用 ESP32-C3 或 M5Stamp C3（GPIO3 电平判断：M5Stamp 按钮上拉读高，通用板悬空下拉读低） |
| SK6812 LED | [main/led.c](main/led.c) | M5Stamp C3 使用 GPIO2 RMT bytes encoder 驱动 RGB LED，颜色映射：蓝=广播/绿=连接/黄=确认 |
| 按钮适配 | [main/button.c](main/button.c) | 引脚改为运行时 `board_btn_pin()` 获取，M5Stamp 用 GPIO3 |
| 配网修复 | [main/wifi_portal.c](main/wifi_portal.c) | 修复 `syncMode()` 多余 `}` 导致 JS 语法错误，模式切换恢复正常 |
| host 单测修复 | [components/bikepower_domain/hr_model.c](components/bikepower_domain/hr_model.c) | 添加 `M_PI` 兼容定义，host 单测编译通过 |

### 真机验证（已通过）

- 板型检测：`自动检测板型：M5Stamp C3（GPIO3=1）`
- SK6812 RGB LED：蓝色慢闪（广播）/ 绿色常亮（连接）/ 黄色快闪（确认）
- BLE 五服务 + FTMS ERG：正常
- 空闲堆：173KB（> 80KB 阈值）
- 配网页面模式切换：三段切换正常
- host 单测：581 通过，0 失败

---

## 阶段9 完成情况（v3.1.4 Captive Portal + OTA 规划）

### 已实现

| 模块 | 文件 | 说明 |
|------|------|------|
| Captive Portal | [main/captive_portal.c](main/captive_portal.c) / [main/captive_portal.h](main/captive_portal.h) | 自研轻量 DNS 服务器（UDP 53 劫持所有 A 查询返回 192.168.4.1）+ OS 探测端点 303 重定向 handler |
| DNS 生命周期 | [main/radio_ctrl.c](main/radio_ctrl.c) | AP 启动后启动 DNS，退出配网时停止（captive_portal_start_dns / captive_portal_stop_dns） |
| 探测端点路由 | [main/wifi_portal.c](main/wifi_portal.c) | 注册 10 个 OS 探测端点（/generate_204、/hotspot-detect.html、/ncsi.txt 等），统一 303 重定向到首页 |
| 分区表 | [partitions.csv](partitions.csv) | v4.0.0 起改为单 factory（3.875MB），仍 4MB Flash（阶段 9 时为 ota_0/ota_1 双 OTA 布局，已随 OTA 移除废弃） |

> 注：本阶段的在线 OTA（4MB 双 OTA 分区 + 回滚 + HTTPS 下载）已于 **v4.0.0 整体移除**，详见阶段 13 与 [docs/archive/07-remove-ota-plan.md](docs/archive/07-remove-ota-plan.md)。以下 Captive Portal 部分仍然有效。

### 覆盖的 OS 探测端点

Android/Chrome、iOS/macOS、Windows、Firefox、Kindle 共 10 个端点，统一 303 重定向触发系统自动弹出 Captive Portal 窗口。

### 资源占用

- DNS 任务栈 3072 字节 + UDP 缓冲 512 字节（栈上）
- 固件增量 ~2KB
- 配网态空闲堆仍 >80KB 告警线

### 待人工确认（需真机验证）

- [ ] iOS 连上 BikePower-XXXXXX 热点后自动弹出配网页
- [ ] Android 连上后自动弹出配网页
- [ ] Windows 连上后浏览器提示需登录
- [ ] 不弹窗时手动访问 192.168.4.1 仍可配网（降级兼容）
- [ ] 退出配网后 DNS 任务已停止（串口日志）
- [ ] 从旧双 OTA 分区切到当前单 factory 分区时需 `esptool.py erase_flash`；当前仍是 4MB Flash。

### OTA 实施（v3.1.6 引入，v4.0.0 已移除）

在线 OTA（家庭 WiFi + SNTP + HTTPS 拉 `latest.json` + 流式 `esp_ota_write` + 自检回滚 + 防降级）曾在 v3.1.6~v3.1.8 实现，已于 **v4.0.0 整体移除**（连同家庭网络配置与双 OTA 分区表）。移除范围与理由见阶段 13 与 [docs/archive/07-remove-ota-plan.md](docs/archive/07-remove-ota-plan.md)。当前固件升级走电脑端网页烧录：项目自建 GitHub Pages 已上线，临时不可用时可使用 `https://easyhey.com/?firmware=MOD220` 备用入口；手机端不支持烧录。分区表变更场景仍需 `erase_flash`。

---

## 已知限制 / 注意事项

- ESP-IDF 已升级至 v5.5.4（修复了 NimBLE host 潜在断连问题；工具链 esp-14.2.0_20260121）。
- WiFi/BLE 共存：配网期间 BLE 保持在线（`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`），仅资源初始化失败时降级为停 BLE + 重启（见 [.trae/rules/wifi-ble-mutex.md](.trae/rules/wifi-ble-mutex.md)）
- 物理按钮/LED 灯效需人工确认，自动化测试只能覆盖 BLE 连接驱动的状态迁移
- 配网门户动态测试（stage5_portal_test.py）需本机连 BikePower-XXXXXX 热点；本机不能切换 WiFi 时改用 `tests/mock_server.py` 本地调试，或换另一台机器执行
- `test/` 与 `tests/` 目录职责不同不合并（评估结论见 [tests/README.md](tests/README.md) 第六章）：`test/` 放 ESP-IDF 约定的 host 单测（纯 C），`tests/` 放所有 Python/Shell 设备测试与静态门禁
- P0 协议与可靠性修复设计见 [归档规格](docs/archive/14-top5-protocol-stability-design.md)，持续待办见 [优化清单](docs/dev/13-project-review-and-optimization.md)。修复实现已完成，硬件和人工验收按当前镜像证据判定，不把自动化全绿等同于全部验收完成。
- GitHub Release、Actions 和 Pages 已收敛为单一链路；v4.1.1 双板固件在线 SHA256 与 manifest 已自动复核，generic-c3 / m5stamp-c3 浏览器真机刷写仍待确认。
