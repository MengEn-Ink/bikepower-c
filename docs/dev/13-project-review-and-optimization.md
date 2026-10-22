# 项目复审与优化清单

> 审查日期：2026-10-22
>
> 基线：BikePower C v4.1.1，提交 `d0ecc8e` 之后的工作区
>
> 范围：`main/`、`components/bikepower_domain/`、`test/`、`tests/`、构建/发布脚本、`docs/` 与 `.trae/` 治理资产

## 一、结论

项目当前具备可重复构建、领域层回归、静态门禁、真机证据和 release 产物校验链路。审查基线结果为：

- `idf.py build` 通过。
- host 单测 5841 条断言通过。
- RF、ISR、日志和开机 WiFi 硬约束未发现直接违规。
- 质量门禁唯一即时失败是 `docs/superpowers` 工作目录被重新引入，违反已生效的文档治理规则。

但“构建和现有测试全绿”不能证明 BLE 协议正确。源码复核确认 5 项 P0 缺陷仍真实存在，其中 CPS 问题同时暴露了“生产实现与真机解析器复制同一错误”的测试同源风险。详细修复设计见 [12-top5-protocol-stability-design.md](12-top5-protocol-stability-design.md)。

## 二、本轮已治理

### 2.1 源码卫生

- DNS 任务栈、UDP 报文上限、TTL、停止等待、网关 IPv4 和 HTTP socket 上限集中到 `main/bikepower_config.h`。
- `captive_portal.c`、`radio_ctrl.c`、`wifi_portal.c` 改为引用集中常量，数值和设备行为不变。
- 移除 `app_main.c` 未使用的标准库 include。
- 清理状态机中“阶段5接入”等已过期注释和日志。
- 修正 `ble_bikepower.c` 顶部注释，使其如实描述当前 9 字节旧兼容 CPS 布局；这只是事实修正，不把缺陷标记为已解决。

### 2.2 文档治理

- 将游离设计稿迁入 `docs/dev/12-top5-protocol-stability-design.md`，恢复 `docs/user|dev|wiki|archive` 分类边界。
- Wiki 首页统一为小写 `00-home.md`。
- 清理文档中的本机绝对 URI，改为仓库相对路径。
- 修正 v4.1.0/v4.0.1 版本残留、18+10 路由数、FTMS OpCode、500ms 广播间隔和 18 字节广播名预算。
- 移除覆盖率文档中易漂移的手工行数，补齐当前 12 个领域层和 12 个板级生产源文件。
- 明确文档权威顺序：源码/配置 → `PROGRESS.md` → 技术参考 → Wiki；归档文档不参与当前事实判定。
- 静态门禁新增规范目录、可移植链接、项目记忆版本、路由计数和本审查文档检查。

### 2.3 验证结果

- `python3 scripts/tools/quality_gate.py`：63 pass / 2 个预期工作区未提交 warning / 0 fail，包含 ESP-IDF 构建和 5841 条 host 断言。
- `python3 tests/operation_gate.py --level quick`：host、mock 保存、操作覆盖、BLE 监控资产、门户结构和门户 JS 全部通过。
- `python3 -m unittest tests/test_serial_daemon.py tests/test_serial_boot_check.py`：3 项通过。
- `git diff --check`：通过。

本轮没有烧录或执行真机测试，因为修改范围不改变设备运行行为；P0 协议修复完成后必须按第六节顺序跑双板真机验收。

## 三、P0：发版前必须修复

### P0-1 BLE payload 标准兼容与独立真值

现状证据：

- `ble_bikepower.c::pack_all()` 将 CPS flags 写入 1 字节，后接 power/rev/reserved/time，共发送 9 字节；标准目标应为 2 字节 flags + power + rev + time，共 8 字节。
- FTMS Indoor Bike 在仅踏频场景仍可能声明 power present。
- FTMS Treadmill 当前 flags 为 `0x0306`，与实际 distance/heart-rate/elapsed-time 字段不一致，目标为 `0x0504`。
- 现有 Python 自测曾按同一错误偏移解析 CPS，因此不能作为独立证据。

优化内容：

1. 新增纯 C `ble_payload_codec`，让固件真正调用可 host 测的编码器。
2. 新增人工维护的固定协议向量，不从生产编码器生成 fixture。
3. C 编码器与 Python 解析器分别对固定字节真值验证。
4. 真机测试继续验证通知链路，但不再独自承担字节布局正确性。

验收：固定向量、host、Python parser、固件构建和骑行/跑步真机通知全部通过。

### P0-2 跑步 FTMS 会话语义

现状证据：

- `Start/Resume`、`Stop/Pause` 和 `Reset` 会向客户端返回 Success。
- 应用事件处理没有运行/暂停状态；`APP_EVT_RUN_START` 和 `APP_EVT_RUN_STOP` 只记录日志。
- 跑步引擎仍持续推进距离和用时，导致协议响应与设备行为不一致。

优化内容：

- `running_engine_t` 增加非持久化 `running` 状态，启动保持现有“默认运行”兼容行为。
- 暂停时速度、步频和步幅归零，距离与用时冻结；恢复后继续累计。
- 控制事件未可靠进入主循环时返回 Operation Failed，不提前返回 Success。

验收：Stop 后两次以上通知窗口内累计冻结，Start 后恢复，Reset 清零且保持原运行/暂停态。

### P0-3 ERG 会话态与持久配置分离

现状证据：

- `power_engine_set_erg_target()` 当前同步覆盖 `base_power`、切换持久模式并置 dirty。
- `nvs_store::engine_to_config()` 直接保存当前 `mode`。
- 设备可能在无控制端的重启后恢复为 ERG 模式。

优化内容：

- ERG target、active 和控制权仅作为运行时会话态。
- 保留进入 ERG 前的固定功率，不因训练 App 高频目标写入触发 NVS。
- 保存时把 ERG 规范化为 steady；加载旧 NVS 的 ERG 值时同样修正并回写。

验收：ERG 目标变化不置 dirty、不改 base power；重启后永不以无控制端 ERG 启动。

### P0-4 WiFi 配网完成事件最终可达

现状证据：

- `on_wifi_done()` 只做有限次数队列投递。
- 全部失败后门户已进入 closing/done 状态，不再重试，设备可永久停在 `ST_WIFI_CONFIG`。

优化内容：

- done 回调返回是否已被可靠接收。
- 队列满时设置单比特 pending latch，由主循环下一拍消费。
- `radio_ctrl_exit_wifi()` 和 `APP_EVT_WIFI_DONE` 保持幂等，错误恢复态也能清理 RF/HTTP/DNS 资源。

验收：注入满队列后最终退出配网；重复完成事件不崩溃、不泄漏、不错误切换异常态。

### P0-5 真机门禁不得把 P0 失败降级为 SKIP

现状证据：

- `operation_gate.py` 将跑步 BLE 和连接事件步骤设置为 `required=False`。
- `stage3_conn_test.py` 只做一次连接/断开，并把本地主动断开直接视为成功，没有第二次扫描和重连。

优化内容：

- `ble_cycling`、`ble_running`、`ble_reconnect` 作为 device 级必选项。
- 显式启用配网测试时，`portal_http`、`broadcast_mask` 同样必选。
- 重连测试执行扫描 → 连接 → 断开 → 再扫描 → 第二次连接 → 第二次断开。

验收：任一 P0 自动项失败时 device gate 返回非零，并在证据 JSON 中如实记录 FAIL。

## 四、P1：P0 收口后实施

### P1-1 大模块拆分

当前高复杂度文件包括 `main/ble_bikepower.c`、`main/wifi_portal.c`、`main/ble_scanner.c` 和 `main/web/app.js`。建议按协议 codec、GATT 表、HTTP 路由/handler、外部设备采样、前端状态/API 适配器分批拆分；每次只拆一个稳定边界，禁止与行为改动混提。

### P1-2 并发状态原语统一

当前多处使用 `volatile` + 短临界区传递单值事件。P0 的 WiFi latch 完成后，应梳理 ERG、BLE ready/stopping 和连接句柄的所有写者/读者，为每个状态明确“单写者、锁、队列或原子”中的一种契约，避免把 `volatile` 误当完整同步机制。

### P1-3 门户响应容量显式化

`wifi_portal.c` 中仍有多组 1.6KB～4KB 静态 JSON 缓冲。建议为 state、template、analysis 三类响应建立命名容量常量和序列化失败测试，并记录配网态最低堆水位；不建议为了减少行数改成运行时频繁分配。

### P1-4 文档事实自动生成

版本、schema、断言数、HTTP 路由数和模块清单属于可机器读取事实。后续优先让门禁校验或生成摘要，避免在多个 Wiki 页面手工复制。技术参考保留行为解释，Wiki 不再维护易漂移的手工行数。

## 五、P2：维护体验优化

- 为 `quality_gate.py` 增加 `--no-build` 或分层入口，保留默认完整门禁，方便纯文档改动快速验证。
- 定期清理本机忽略产物；本次审查时 `build/` 约 1.9GB、`tests/reports/` 约 7MB，但它们均未被 Git 跟踪，因此不属于源码污染。
- 当前门禁已检查本机绝对 URI 和仓库内相对链接目标；后续可增加 Markdown 锚点有效性校验。
- 评估把历史实施计划从 `docs/dev/` 迁入 `docs/archive/`，但需与 release 门禁硬编码路径一起单独治理，避免混入协议修复提交。

## 六、推荐实施顺序

1. P0-1 codec + golden vectors。
2. P0-2 跑步会话状态。
3. P0-3 ERG 持久化边界。
4. P0-4 WiFi 完成事件可靠性。
5. P0-5 device gate 和双次重连。
6. 无设备全量门禁。
7. generic-c3 真机 P0。
8. m5stamp-c3 真机 P0。
9. 物理按钮/LED、Captive Portal 多系统弹窗和 72h 老化。

每个 P0 单独提交并记录真实测试结果。协议修复不得与文档目录整理或纯格式化混在同一提交中。
