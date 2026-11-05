# 自动优化实施计划

> 归档于 2026-11-05：自动化实施已完成。当时结果及待人工验收项见 [自动优化验证报告](26-automatic-optimization-report.md)。

日期：2026-11-04。基线：`cf1b462`，v4.2.1。用户授权先完成除人工操作外的任务；固件须用户手动验证并明确批准后才能发布。

## 方案与边界

采用局部拆分与统一同步契约：配置事务从 BLE 运动服务移到独立板级模块，保留应用层公开 API、UUID、六组协议、NVS schema 和运动算法。共享快照使用短临界区整体复制；生命周期单值使用 C11 原子；协议栈停用仍由已有通知互斥锁保护。临界区内不调用日志、JSON 序列化、BLE、HTTP 或 NVS；固定长度纯 C payload codec 只进行有限字节写入。

其他选择：仅完善文档和工具可减少固件改动，但不能完成并发与模块整理；全面重写 BLE/WiFi 架构影响面过大，不采用。保持当前模块边界和公开行为，逐批验证。

## 批次一：维护工具

- [x] `scripts/tools/quality_gate.py` 增加 `--no-build` 与 `--level docs`；默认仍执行完整门禁，跳过项明确展示，不计为构建通过。
- [x] `scripts/tools/doc_facts.py` 从源码生成版本、schema、生产模块及 HTTP 路由摘要；`--check` 检查生成文档漂移，不维护手工断言数。
- [x] `scripts/tools/check_markdown_links.py` 验证仓库文档相对链接及 Markdown 标题锚点，处理重复标题、代码块、百分号编码和引用式链接。
- [x] 先验证失败用例，再实现工具，运行功能测试和文档门禁。

## 批次二：BLE 结构与并发

- [x] `main/ble_config_service.c/.h` 负责六组 GATT 配置快照、事务、revision、控制响应和生命周期重置；`ble_bikepower` 保留兼容包装接口。
- [x] `main/ble_bikepower.c` 将模拟、桥接、跑步输入与掩码统一整体复制，单轮打包与通知使用同一份快照；GATT 读取与通知缓冲更新使用短临界区。
- [x] 生命周期 ready/stopping/failed/连接句柄使用 C11 原子；应用 mailbox 明确队列优先、临界区 fallback，radio_ctrl 的暂停态明确单主任务所有权。
- [x] 实际生产配置回调与通知逻辑用宿主机替身验证；运行协议向量、host、ASan/UBSan、BLE/配置/重连/running 契约与 ESP-IDF 构建。

## 批次三：门户容量与并发

- [x] `main/bikepower_config.h` 集中 state/runtime/scan JSON 输出容量；仅调整工程容量，不改变用户配置或 schema。影响：`wifi_portal.c` 与相关容量门禁、技术参考。
- [x] `main/wifi_portal.c` 使用 HTTP 任务独占静态序列化缓冲，`cJSON_PrintPreallocated` 显式处理容量不足；配置合并使用已有 `CONFIG_JSON_MAX_LEN`。
- [x] 超时、关闭、done 和重启请求使用同一短临界区，重启请求原子取清，done 回调在锁外执行且保留重试。
- [x] 生产门户宿主机测试覆盖容量超限、最长字符串、关闭/续期与 done 重试；运行 HTTP mock、前端交互和相关静态门禁。

## 批次四：文档治理与验证

- [x] 同步 README、技术参考、领域规则、PROGRESS 和 CHANGELOG；旧 180 秒及固件命名说明按当前行为修正，历史记录注明历史口径。
- [x] 已完成实施计划迁入 archive，并同步索引、相对链接和硬编码门禁；持续优化清单保留 dev。
- [x] 根据已批准门户设计补清理无构建/调用方的旧 `device_template_store.c/.h`，同步当前模块文档；不擦除历史 NVS。
- [x] 本机缓存已盘点，无确认可删目标；保留候选固件、证据、用户原型与并行 Pages 资产，不强行清理。
- [x] 跑完整质量/操作门禁、双板构建；generic-c3 烧录、启动、配置/NVS、cycling/running、双次重连与 307 秒耐久通过，独立镜像证据标记 dirty。
- [x] 收尾发现 CI harness 的 C11/POSIX 声明问题，补齐四个编译入口并执行 GCC 严格告警与实际 ASan/UBSan 回归；远端 CI 推送后验证。

## 人工及硬件依赖

真实功率计与下游 App/WiFi 三方共存、M5 实物、手机/Safari、按钮/LED、原生 Web Bluetooth/Web Serial 授权、系统配网弹窗及可选 72h 老化保持待办。无设备或关联失败不视为 PASS。既有候选证据不能直接挪作本轮固件通过证据。

本轮按主题保留可分别提交的变更；未获得新的提交/发布指令时不创建 commit、tag 或 Release。
