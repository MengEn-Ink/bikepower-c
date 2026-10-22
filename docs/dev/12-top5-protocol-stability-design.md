# Top 5 缺陷与 BLE 协议稳定性设计规格

## 1. 背景

2026-08-21 全仓审查确认项目当前可以稳定编译，质量门禁和领域层单测均通过，但存在五项高优先级缺陷：

1. CPS Measurement 编码和真机自测共同使用错误字段偏移。
2. 跑步 FTMS Start/Resume、Stop/Pause 返回成功，却没有实际启停会话。
3. ERG 会话态触发 NVS 落盘，并可能在重启后恢复为无控制端的 ERG 模式。
4. WiFi 配网结束事件在应用队列拥塞时可能永久丢失。
5. `operation_gate.py` 将两个 P0 BLE 验收项按可选项处理，失败后仍可能返回成功。

其中 CPS 问题说明现有测试存在“实现和测试复制同一错误”的结构性风险。因此本轮不只做局部补丁，还要建立独立协议 golden vectors，作为后续 BLE 协议优化的稳定基线。

## 2. 目标

- 修复上述 Top 5 缺陷。
- 跑步引擎保持“启动即运行”的兼容行为。
- FTMS Stop/Pause 后冻结跑步距离和用时，Start/Resume 后恢复。
- ERG 激活态、目标和控制模式不进入持久化配置。
- WiFi 完成通知具备可重试、幂等和最终可清理语义。
- P0 自动验收失败必须让 device 门禁返回非零。
- 用规范定义的固定字节向量独立验证固件编码和 Python 解析器。
- 顺带修复 golden vectors 暴露的 FTMS Bike/Treadmill flags 与 payload 不一致问题。

## 3. 非目标

- 不升级 NVS schema 版本。
- 不重写整个应用状态机。
- 不实现完整 FTMS 训练状态机或控制权仲裁。
- 不在本轮重构 BLE scanner 的异步连接状态机。
- 不在本轮处理门户响应缓冲、任务栈或 Kconfig 内存裁剪。
- 不改变 WiFi/BLE 共存主路径。
- 不把 Bluetooth 规范文档或测试 fixture 编译进固件。

## 4. 方案选择

### 4.1 方案 A：直接修改五处实现

优点是改动少、交付快。缺点是协议编码仍位于 `ble_bikepower.c`，Python 自测仍可复制相同偏移错误，不能解决本次缺陷的根因。

### 4.2 方案 B：纯 C codec + 独立 golden vectors

把动态 BLE payload 的字节布局提取到领域层纯 C codec。测试目录维护人工审阅的 JSON golden vectors；固件编码器和 Python 解析器分别与同一份固定期望字节比对。

优点：

- 编码逻辑可在 host 上直接测试。
- 规范字节不由生产实现生成，避免自证正确。
- Python 真机脚本、监控脚本和固件编码器可分别发现漂移。
- 后续增加字段或修复 flags 时有明确兼容门禁。

缺点是新增一个小型 codec 模块、测试 CLI 和协议 fixture。

### 4.3 方案 C：全面拆分 BLE/WiFi/运动状态机

长期结构最好，但一次涉及连接、配网、控制模式和会话状态，修改面过大，不适合与协议兼容修复混在同一轮。

### 4.4 决策

采用方案 B。状态机只做支撑 Top 5 所需的局部调整，不引入新的架构层。

## 5. BLE payload codec

### 5.1 模块边界

新增：

- `components/bikepower_domain/include/ble_payload_codec.h`
- `components/bikepower_domain/ble_payload_codec.c`

该模块：

- 只依赖 C11 标准类型和领域枚举。
- 不依赖 ESP-IDF、FreeRTOS、NimBLE 或堆分配。
- 接受显式字段和调用方缓冲区。
- 成功返回实际 payload 长度，参数或容量非法返回 0。
- 只负责字段布局、flags 和小端编码，不维护累计值或时钟。

`ble_bikepower.c` 继续负责：

- 曲柄累计圈数和事件时间推进。
- 当前运行快照读取。
- NimBLE mbuf 分配和通知。
- 广播项开关。

### 5.2 CPS Measurement

骑行功率通知使用 Crank Revolution Data：

| 偏移 | 长度 | 字段 |
|---|---:|---|
| 0 | 2 | Flags = `0x0020` |
| 2 | 2 | Instantaneous Power，`sint16` |
| 4 | 2 | Cumulative Crank Revolutions，`uint16` |
| 6 | 2 | Last Crank Event Time，`uint16`，1/1024 秒 |

总长度固定为 8 字节。

这是一项协议兼容修复。旧的 9 字节非标准布局不再保留兼容分支，否则标准客户端与旧错误客户端无法同时获得正确字段。

### 5.3 CSC Measurement

使用 crank-only 格式：

- Flags = `0x02`
- Cumulative Crank Revolutions = 2 字节
- Last Crank Event Time = 2 字节
- 总长度 5 字节

### 5.4 FTMS Indoor Bike Data

根据广播 mask 生成以下组合：

| 功率 | 踏频 | Flags | Payload |
|---|---|---|---|
| 开 | 开 | `0x0044` | speed + cadence + power |
| 关 | 开 | `0x0004` | speed + cadence |
| 开 | 关 | `0x0041` | power |

`More Data` 位只在省略 Instantaneous Speed 时设置。`Instantaneous Power Present` 位只在实际追加功率字段时设置。

### 5.5 FTMS Treadmill Data

当前发送 speed、total distance、heart rate、elapsed time，因此 Flags 固定为：

```text
0x0004 Total Distance Present
0x0100 Heart Rate Present
0x0400 Elapsed Time Present
合计 0x0504
```

payload 顺序为 flags、instantaneous speed、total distance、heart rate、elapsed time，总长度 10 字节。

### 5.6 HRS 与 RSC

HRS 保持 `flags + bpm`。

RSC 根据跑步会话状态设置 Walking or Running Status 位：

- 运行态：flags `0x07`，输出速度、步频、步幅、累计距离。
- 暂停态：flags `0x03`，速度、步频、步幅为 0，累计距离保持。

## 6. 独立协议 golden vectors

### 6.1 文件

新增：

- `tests/fixtures/ble_protocol_vectors.json`
- `tests/ble_protocol.py`
- `tests/test_ble_protocol_vectors.py`
- `test/ble_codec_cli.c`

### 6.2 真值边界

`ble_protocol_vectors.json` 是人工维护、按 Bluetooth 字段定义审阅的固定真值，至少覆盖：

- CPS 正功率、负功率、曲柄计数和事件时间。
- CSC crank-only。
- FTMS Bike 全开、仅踏频、仅功率。
- FTMS Treadmill 距离、心率和用时。
- HRS 8 位心率。
- RSC 运行与暂停。

fixture 包含显式输入、期望十六进制 payload 和期望解码字段。生产代码和 Python 解析器都不得生成 fixture。

### 6.3 C codec 校验

`ble_codec_cli.c` 链接领域层 codec，接受协议名和数字字段，输出十六进制 payload。Python 测试逐条读取 fixture、调用 CLI，并把输出与 `expected_hex` 精确比较。

这样可以验证：

- flags 位。
- 字段顺序。
- 有符号和无符号编码。
- 小端字节序。
- 精确包长。

### 6.4 Python 解析器校验

`tests/ble_protocol.py` 提供无 bleak 依赖的解析函数。`ble_selftest.py` 和 `ble_broadcast_monitor.py` 复用这些函数。

Python 测试直接把 fixture 的固定字节交给解析器，检查结果与 fixture 的期望字段一致。编码器和解析器因此是两条独立验证路径。

## 7. 跑步会话状态

### 7.1 数据结构

`running_engine_t` 增加 `bool running`，初始化为 `true`。

`running_snapshot_t` 增加 `bool running`，供 BLE RSC codec 设置 Walking or Running Status 位。

新增领域接口：

```c
bool running_engine_set_running(running_engine_t *e, bool running);
```

`power_engine` 增加对应薄封装，不把运行/暂停视为持久配置，不置 `dirty`。

### 7.2 状态语义

- 启动或 profile 切换到 running：默认运行。
- Start/Resume：设置 `running=true`。
- Stop/Pause：设置 `running=false`。
- Reset：距离和用时清零，保持当前运行/暂停状态。
- 暂停期间每 50ms 仍生成快照，保证 BLE 可持续通知，但不推进累计值。
- 暂停快照速度、步频和步幅为 0，距离和用时保持最后值。
- 恢复后使用当前配置继续生成速度、步频和心率。

### 7.3 FTMS 控制点响应

Start/Stop/Reset 只有在应用事件成功进入可靠处理通道后才返回 Success。事件未接受时统一返回 FTMS Operation Failed（`0x04`），不提前更新 FTMS Status。Invalid Parameter（`0x03`）只用于带参数命令的值域错误。

本轮不等待主循环同步执行完成；“成功”表示命令已被应用层可靠接受，符合当前异步架构。

## 8. ERG 会话态与持久化边界

### 8.1 运行时状态

`power_engine_set_erg_target()`：

- 校验目标范围。
- 写入 `erg_active_target` 和 `has_erg_target`。
- 运行时切换为 `RIDE_MODE_ERG`。
- 不覆盖持久化 `base_power`。
- 不设置 `dirty`。

`power_engine_clear_erg_target()`：

- 清除 ERG 目标。
- 切回 `RIDE_MODE_STEADY`。
- 恢复 ERG 前的 `base_power` 输出。
- 不设置 `dirty`。

### 8.2 保存规范化

即使用户在 ERG 会话中修改了其他持久配置并触发保存，`engine_to_config()` 也必须将运行时 ERG 规范化为 `RIDE_MODE_STEADY`，并保存原有 `base_power`。

### 8.3 旧 NVS 兼容

`config_store_validate()` 将持久配置中的 `RIDE_MODE_ERG` 规范化为 `RIDE_MODE_STEADY`。现有 schema 字段没有变化，因此 `CONFIG_VERSION` 保持 v11。

加载后若发生该修正，沿用现有 `fixed -> nvs_store_save()` 路径回写，不保留错误 ERG 值。

### 8.4 配置 API 边界

配网页只允许持久设置 `steady` 和 `road`。`erg` 只作为 `/api/state` 的运行态输出，不作为可保存输入。

## 9. WiFi 完成通知可靠性

### 9.1 回调契约

`wifi_portal_set_done_callback()` 的回调改为返回 `bool`：

- `true`：完成通知已被应用层可靠接受。
- `false`：尚未接受，门户必须保留重试能力。

`portal_done()` 立即设置 `s_closing=true`，阻止续期和新配置提交；只有回调返回 `true` 后才设置 `s_done_called=true`。

### 9.2 重试

周期定时器在 `s_closing && !s_done_called` 时继续调用 `portal_done()`。回调缺失或应用事件暂时无法接受时记录限频错误，但不永久关闭重试。

`on_wifi_done()` 先尝试投递 `APP_EVT_WIFI_DONE`；队列满时在临界区内设置 `s_pending_wifi_done=true`，由主循环下一拍消费。回调只要成功入队或成功设置 latch 就返回 `true`。latch 只表示“存在一个 WiFi 完成事件”，不保存计数，因为该事件本身幂等。

### 9.3 幂等资源清理

`APP_EVT_WIFI_DONE` 不再只依赖 `s_state == ST_WIFI_CONFIG`。处理前记录当前是否为 `ST_ERROR_RECOVERY`，随后无条件调用幂等的 `radio_ctrl_exit_wifi()`。若原状态为错误恢复态，则继续保持 `ST_ERROR_RECOVERY` 和错误 LED；否则切换为 `ST_BLE_ACTIVE` 并刷新空闲 LED。

`radio_ctrl_exit_wifi()` 继续保持可重复调用。

### 9.4 启动失败路径

HTTP 或 timer 初始化失败时，同样使用上述可靠完成通道。不得假设一次队列投递必然成功，也不得依赖尚未创建的 timer 才能完成资源清理。

## 10. P0 device 门禁

### 10.1 必选项

以下自动验收项是 P0，执行后失败必须进入 `Gate.failures`：

- `ble_cycling`
- `ble_running`
- `ble_reconnect`
- `portal_http`（仅显式启用 portal 真机测试时）
- `broadcast_mask`（仅显式启用 portal 真机测试时）

未请求 portal 测试时，相关项保持 `NOT_RUN`，不能伪装成 PASS。

### 10.2 BLE 重连

`stage3_conn_test.py` 必须执行：

1. 扫描并第一次连接。
2. 主动断开。
3. 再次扫描设备。
4. 第二次连接成功。
5. 第二次断开。

发布级重连结论不能仅依赖第一次连接或本地 `disconnected_ok=true`。

### 10.3 跑步真机测试

running profile 自测增加：

- 启动后 RSC 有非零速度并累计距离。
- Stop/Pause 响应成功后，速度归零且距离、用时在观察窗口内冻结。
- Start/Resume 响应成功后，速度恢复且累计继续增长。
- Reset 后累计清零，并保持 Reset 前的运行/暂停状态。

## 11. 测试策略

所有实现按 TDD 执行，每项先产生针对当前缺陷的失败结果。

### 11.1 Host

- BLE codec fixture 精确字节测试。
- Python 解析器 fixture 测试。
- 跑步初始化、暂停、恢复、Reset 状态测试。
- ERG 目标变化不置 dirty、base power 不变测试。
- `config_store_validate()` 将持久 ERG 修正为 steady。
- ERG 会话期间配置导出仍保存 steady/base power。

### 11.2 静态与 Python 工具

- operation gate 单测确认 P0 BLE 失败进入 failures。
- 重连脚本结构门禁确认存在第二次扫描和第二次连接。
- WiFi 完成回调签名、成功后置 done 标志和 pending fallback 契约测试。

### 11.3 集成

- `make -C test run`
- `python3 tests/test_ble_protocol_vectors.py`
- 门户、配置、NVS、任务/WDT 和 operation gate 全量静态门禁。
- `idf.py build`
- 双板构建前至少验证 generic-c3 和 m5stamp-c3 的配置生成路径。

### 11.4 真机

- generic-c3：骑行 BLE、跑步 BLE、连接重连、配网退出。
- m5stamp-c3：同一组 P0 验收。
- CPS 修复后至少用一个标准骑行客户端确认功率字段。
- 配网退出测试包含人为制造事件队列压力或 pending fallback 证据。

无法取得硬件时，代码与 host/静态验证可完成，但不得将 device evidence 标记为 PASS。

## 12. 文档同步

必须同步：

- `.trae/rules/domain/ble.md`：CPS 改为 crank rev 2 字节，总长 8 字节；补 FTMS flags 契约。
- `docs/dev/01-technical-reference.md`：跑步会话、ERG 运行态和 WiFi 完成重试。
- `docs/dev/04-running-mode-compatibility.md`：启动即运行、Stop/Start 行为。
- `docs/dev/05-operation-test-gate.md`：P0 running/reconnect 必选语义。
- `tests/README.md`：golden vectors 和重连测试说明。
- `CHANGELOG.md`、`PROGRESS.md`：代码修改与验证结果。

## 13. 分批交付

按项目提交规则拆分：

1. 设计文档批：本规格。
2. 协议测试批：golden fixture、Python parser、host codec runner，先形成 RED。
3. 协议实现批：纯 C codec、CPS/FTMS 修复。
4. 跑步与 ERG 领域修复批：会话状态和持久化边界。
5. WiFi 生命周期修复批：可靠完成通知和幂等清理。
6. 门禁工具批：P0 required、真实重连与跑步验收。
7. 文档同步批：规则、技术文档、CHANGELOG、PROGRESS。

每批使用指定路径暂存，完成对应测试后提交并推送，不混入无关重构。

## 14. 验收标准

- 所有 golden vectors 对 C codec 和 Python parser 均通过。
- CPS payload 精确为 8 字节，标准偏移功率正确。
- FTMS Bike/Treadmill flags 与实际 payload 长度一致。
- 跑步启动即运行；Stop 冻结；Start 恢复；Reset 语义稳定。
- ERG 目标变化不触发配置 dirty，NVS 永不保存 ERG 模式。
- WiFi 完成事件在队列满时最终仍能触发幂等资源清理。
- `ble_running` 或 `ble_reconnect` 执行失败时 device gate 返回非零。
- 全量 host、静态门禁和 ESP-IDF 编译通过。
- 两板型真机证据未完成时明确保留 `NOT_RUN`，不伪造通过。
