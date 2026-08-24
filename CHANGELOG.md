# 更新日志

本项目遵循 SemVer。详细历史通过 Git tag 和提交记录查询；本文只保留当前未发布变更与最新稳定版摘要。

## [未发布]

### 协议与状态

- 新增纯 C BLE payload codec 和 10 组固定协议向量。
- CPS 修正为标准 8 字节 crank 数据布局。
- 补齐 Cycling Power Feature、CSC Feature 和 Sensor Location。
- RSC Feature 修正为 `0x0007`，累计距离使用 0.1m 分辨率。
- FTMS Indoor Bike/Treadmill flags、字段与长度按规范对齐。
- 跑步 Stop/Start/Reset 具备真实暂停、恢复和清零语义。
- ERG 改为非持久会话态；历史持久 ERG 自动规范化为 steady。
- ERG target/clear 队列满时保留最后命令，避免顺序反转。
- WiFi 完成事件增加可确认回调、重试和 pending latch。

### 内存、交互与构建

- Portal 复用单个 4KB 响应缓冲，大响应改为 chunked 发送。
- `libmain.a` `.bss` 从 27863 字节降至 12594 字节，减少 15269 字节。
- profile 等待 BLE 断开重建时，网页显示明确提示。
- flash、release 与 quality gate 使用独立构建目录和 sdkconfig。
- ccache 改用 ESP-IDF 官方 `IDF_CCACHE_ENABLE=1`。
- 删除重复 wiki、历史方案、已完成计划、旧测试报告、日志和未引用 SVG。
- 当前文档收敛到 `README.md`、`PROGRESS.md`、`docs/user/`、`docs/dev/` 与 `tests/README.md`。
- `PROGRESS.md`、技术参考、测试指南与 `.trae` 规则/记忆改为当前事实版；源码删除模板化函数注释，只保留协议、并发、ISR、锁、WDT 和硬件约束。
- 新增 `tests/check_project_hygiene.py`，阻止历史目录、旧报告、日志、测试二进制和未引用 SVG 回流。
- 真机验收固件支持隔离的 `cycling` / `running` profile 覆盖，不修改 NVS；device gate 只判定当前声明的 profile。
- 运行报告与烧录日志改写到 `build/`，避免测试执行后反向污染源码树。
- ccache 增加统一 `CCACHE_BASEDIR` 与 `CCACHE_NOHASHDIR=true`；实测第二个全新构建目录命中 1048 次。
- WiFi 完成 timer 回调改为零等待投递；ERG target/clear 始终走同一 last-command mailbox，消除队列交错顺序窗口。
- BLE 骑行/跑步输入与编码缓冲增加短临界区，保证通知和 GATT read 使用同一帧快照。

### 验证

- `make -C test run`：5866 通过，0 失败。
- `python3 tests/test_ble_protocol_vectors.py`：10 个向量双路径通过。
- `python3 tests/operation_gate.py --level full`：通过，包含 6 轮核心流程耐久。
- `python3 scripts/tools/quality_gate.py`：82 通过，0 告警，0 失败；新增门禁全部接入后的最终结果见提交说明。
- `idf.py build`：通过，固件 `0x116250`，app 分区剩余 72%。
- generic-c3 已分别用 cycling/running 测试固件验证两套 GATT profile；running 的 RSC/FTMS、12km/h 目标、Stop 冻结、Start 恢复与 Reset 全部通过。
- Portal 测试固件启动后 BLE 骑行链路保持正常；本机企业网络扩展将 `192.168.4.1` 强制路由到 `utun4`，HTTP/广播项/退出测试保持 `NOT_RUN`。
- 修正 BLE 自测在 CPS、CSC 同时暴露 Sensor Location 时按 UUID 读取产生歧义的问题，并新增回归门禁。
- 质量门禁明确区分未配置 upstream、存在未推送提交与已同步状态。

## [4.1.1] - 2026-08-11

- 移除跑步坡度配置、NVS、Portal 和控制点残留，schema 升至 v11。
- 建立板型、版本和 Git ID 绑定的真机证据链与 release 强校验。
- 完成 generic-c3 发布时的启动、骑行 BLE、ERG 和连接验证。
- m5stamp-c3、按钮/LED、手机 Captive Portal 与 72 小时老化仍需按当前提交重新验收。

更早版本请查看 Git tag 和提交历史。
