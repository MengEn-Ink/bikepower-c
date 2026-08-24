# 项目审查清单

## 代码

- 模块职责单一，无跨层静态状态访问。
- 领域层保持纯 C。
- BLE/WiFi 启停只经过 `radio_ctrl`。
- ISR、NimBLE 与 timer 回调不阻塞、不做重清理。
- 协议字段、flags、长度和单位由 golden vectors 锁定。
- NVS 只保存长期配置，不保存连接或训练会话态。
- 大缓冲复用；任务栈和堆优化必须基于实测。
- 无 `printf`、无无界等待、无忽略返回值的关键路径。

## 文档

- 当前事实只写在 `README.md`、`PROGRESS.md`、`docs/user`、`docs/dev`。
- 历史只写 `CHANGELOG.md`，不新增 archive/wiki/已完成计划副本。
- 参数、版本、schema、路由和测试命令与源码一致。
- 删除功能时同步删除代码、配置、NVS、UI、测试和文档残留。

## 验证

- 领域层：`make -C test run`。
- 协议：`python3 tests/test_ble_protocol_vectors.py`。
- 提交前：`python3 tests/operation_gate.py --level full`。
- 全项目：`python3 scripts/tools/quality_gate.py`。
- 硬件行为：烧录、启动日志、骑行/跑步 BLE、重连、配网和人工按钮/LED。
- 未执行项保持 `NOT_RUN`，不得默认通过。
